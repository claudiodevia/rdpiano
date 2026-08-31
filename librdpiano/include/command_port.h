#ifndef COMMAND_PORT_H
#define COMMAND_PORT_H

#include <stddef.h>

#include "mame_utils.h"

// El protocolo interno del RD-1000 (REFACTORIZACION §3, §17.4).
//
// Antes de la fase 1 estos bytes aparecían crudos en tres capas: 15 `push` en
// el plugin, 6 en el harness, 1 en el standalone y 7 en el propio núcleo. El
// resultado era que el harness *reimplementaba* el arranque en vez de
// ejecutarlo, y que la codificación del master tune estaba duplicada literal.
//
// Aquí vive la única definición de cada mensaje. Lo que el resto del código
// dice es la intención —"cambia de parche", "afina"—, no los bytes.
//
// Lo que NO vive aquí es la temporización: `boot()` y `setMasterTune()` tienen
// que correr la CPU entre mensajes, así que son de `Mcu`, que sí sabe hacerlo.

// Codificación del master tune. La escala es 16 pasos de 4 unidades sobre el
// rango de un int16: `tune/32767*16` truncado, ×4, tope en 0x3c. Los negativos
// van con MSB 0x7f y el LSB desplazado 0x48 arriba, que es como el firmware
// espera el complemento. Estos tres números —0x3c, 0x48, ×16×4— estaban sin
// explicar en dos copias del plugin; ahora están explicados una vez.
struct MasterTuneBytes
{
  u8 msb;
  u8 lsb;
};

MasterTuneBytes encode_master_tune(int16_t tune);

// Cola de bytes hacia el firmware.
//
// Anillo de tamaño fijo: `std::queue` reservaba memoria desde el hilo de audio
// (FIABILIDAD §12). La capacidad da para el mensaje más largo que emite el
// puerto —`allNotesOff()`, 128 notas × 3 bytes— con margen de sobra.
//
// Al desbordar se descarta **el byte nuevo**, no el viejo: la cola es un flujo
// que el firmware lee en orden, y tirar por delante partiría un mensaje ya
// empezado dejando sus bytes restantes a merced del siguiente. Descartando por
// detrás, lo que el firmware ya está leyendo sigue siendo coherente. Los
// descartes se cuentan: si `dropped()` no es cero, algo va mal aguas arriba.
class CommandQueue
{
public:
  static constexpr size_t CAPACITY = 1024;

  bool push(u8 byte)
  {
    if (m_count == CAPACITY)
    {
      m_dropped++;
      return false;
    }
    m_buf[m_tail] = byte;
    m_tail = (m_tail + 1) % CAPACITY;
    m_count++;
    return true;
  }

  bool empty() const { return m_count == 0; }
  size_t size() const { return m_count; }
  u8 front() const { return m_buf[m_head]; }

  void pop()
  {
    if (m_count == 0)
      return;
    m_head = (m_head + 1) % CAPACITY;
    m_count--;
  }

  void clear()
  {
    m_head = m_tail = m_count = 0;
  }

  // Bytes descartados por desbordamiento desde que existe el puerto.
  size_t dropped() const { return m_dropped; }

private:
  u8 m_buf[CAPACITY] = {0};
  size_t m_head = 0;
  size_t m_tail = 0;
  size_t m_count = 0;
  size_t m_dropped = 0;
};

// Los mensajes, por intención. Cada uno es la única definición de sus bytes.
class CommandPort
{
public:
  // 0x30 | n. Además de elegir parche, es lo que hace que el firmware relea
  // la página de parámetros: por eso el arranque y el cambio de parche lo
  // usan aunque la página siempre se mapee como el parche 0.
  void programChange(u8 patch) { m_queue.push(0x30 | (patch & 0x0f)); }

  // 0x31 seguido de 0x30: fuerza un cambio de parche de ida y vuelta para que
  // el firmware vuelva a leer la página que se acaba de mapear.
  void reloadPatch()
  {
    programChange(1);
    programChange(0);
  }

  void masterTune(int16_t tune)
  {
    MasterTuneBytes t = encode_master_tune(tune);
    m_queue.push(0xE0);
    m_queue.push(t.msb);
    m_queue.push(t.lsb);
  }

  void noteOn(u8 note, u8 velocity)
  {
    m_queue.push(0xC0);
    m_queue.push(note);
    m_queue.push(velocity);
  }

  void noteOff(u8 note)
  {
    m_queue.push(0xB0);
    m_queue.push(note);
    m_queue.push(0x00);
  }

  void sustain(bool on) { m_queue.push(0x50 | (on ? 0x0f : 0x00)); }

  // Pánico: suelta el pedal y apaga las 128 notas. Hasta la fase 1 no existía
  // —no había dónde ponerlo— y por eso CC 120/123 se siguen ignorando en
  // `sendMidiCmd` (FIABILIDAD §3): eso es un cambio de comportamiento y va con
  // su prueba en la fase 2. Lo que existe ya es el sitio.
  void allNotesOff()
  {
    sustain(false);
    for (int note = 0; note < 128; note++)
      noteOff((u8)note);
  }

  CommandQueue &queue() { return m_queue; }
  const CommandQueue &queue() const { return m_queue; }

private:
  CommandQueue m_queue;
};

#endif
