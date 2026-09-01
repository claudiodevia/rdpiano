#ifndef COMMAND_PORT_H
#define COMMAND_PORT_H

#include <stddef.h>

#include "mame_utils.h"

// El protocolo interno del RD-1000: la única definición de cada mensaje. Fuera
// de aquí se habla por intención —"cambia de parche", "afina"—, no por bytes.
//
// La temporización no está aquí: `boot()` y `setMasterTune()` tienen que correr
// la CPU entre mensajes, así que son de `Mcu`.

// Codificación del master tune: 16 pasos de 4 unidades sobre el rango de un
// int16 (`tune/32767*16` truncado, ×4, tope en 0x3c). Los negativos van con MSB
// 0x7f y el LSB desplazado 0x48 arriba, que es como lo espera el firmware.
struct MasterTuneBytes
{
    u8 msb;
    u8 lsb;
};

MasterTuneBytes encode_master_tune(int16_t tune);

// Cola de bytes hacia el firmware. Anillo de tamaño fijo (cero reservas en RT)
// con sitio de sobra para el mensaje más largo, `allNotesOff()`.
//
// Al desbordar se descarta **el byte nuevo**: tirar por delante partiría un
// mensaje que el firmware ya está leyendo. Los descartes se cuentan en
// `dropped()`; si no es cero, algo va mal aguas arriba.
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

    void clear() { m_head = m_tail = m_count = 0; }

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

    // Pánico: suelta el pedal y apaga las 128 notas. Ojo: `sendMidiCmd` no lo
    // ata a CC 120/123, que se siguen ignorando.
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
