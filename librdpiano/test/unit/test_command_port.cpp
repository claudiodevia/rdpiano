// El protocolo del firmware (REFACTORIZACION §3, §17.4).
//
// Estas pruebas no necesitan ROMs ni CPU: el puerto es una cola de bytes con
// nombres. Lo que fijan es la única definición de cada mensaje, después de que
// la fase 1 la recogiera de las tres capas por las que estaba repartida.

#include <string>
#include <vector>

#include "command_port.h"
#include "unit_test.h"

// Vacía el puerto y devuelve los bytes en el orden en que los leería la CPU.
static std::vector<u8> drain(CommandPort &port)
{
    std::vector<u8> out;
    CommandQueue &q = port.queue();
    while (!q.empty())
    {
        out.push_back(q.front());
        q.pop();
    }
    return out;
}

static void check_bytes(CheckRun &checks, const char *name, const std::vector<u8> &got, const std::vector<u8> &want)
{
    std::string g, w;
    for (u8 b : got)
        g += check_fmt("%02x ", b);
    for (u8 b : want)
        w += check_fmt("%02x ", b);
    checks.add(name, got == want, check_fmt("emitido [%s], esperado [%s]", g.c_str(), w.c_str()));
}

// ---------------------------------------------------------------------------
// Los mensajes, uno a uno. Los bytes son los que estaban escritos a mano en
// PluginProcessor.cpp, e2e.cpp, standalone.cpp y sendMidiCmd.

TEST_SUITE(command_port_messages)
{
    CommandPort port;

    port.programChange(0);
    check_bytes(checks, "programChange(0)", drain(port), {0x30});

    port.programChange(1);
    check_bytes(checks, "programChange(1)", drain(port), {0x31});

    port.programChange(0xf);
    check_bytes(checks, "programChange(15)", drain(port), {0x3f});

    // El nibble de parche no puede desbordar al byte de comando.
    port.programChange(0xff);
    check_bytes(checks, "programChange satura el nibble", drain(port), {0x3f});

    port.reloadPatch();
    check_bytes(checks, "reloadPatch", drain(port), {0x31, 0x30});

    port.noteOn(60, 100);
    check_bytes(checks, "noteOn", drain(port), {0xC0, 60, 100});

    port.noteOff(60);
    check_bytes(checks, "noteOff", drain(port), {0xB0, 60, 0x00});

    port.sustain(true);
    check_bytes(checks, "sustain on", drain(port), {0x5f});

    port.sustain(false);
    check_bytes(checks, "sustain off", drain(port), {0x50});

    port.masterTune(0);
    check_bytes(checks, "masterTune(0)", drain(port), {0xE0, 0x00, 0x00});
}

// ---------------------------------------------------------------------------
// La codificación del master tune, que estaba duplicada literal en dos sitios
// del plugin sin un solo comentario. La tabla es la de §17.4.

TEST_SUITE(command_port_master_tune)
{
    struct Case
    {
        int16_t tune;
        u8 msb;
        u8 lsb;
    };

    // Valores calculados con las mismas cuatro líneas del plugin anterior a la
    // fase 1: 16 pasos de 4, tope 0x3c, negativos con MSB 0x7f y LSB +0x48.
    const Case cases[] = {
        {0, 0x00, 0x00},      {1, 0x00, 0x00},      {2047, 0x00, 0x00},   {2048, 0x00, 0x04}, {16383, 0x00, 0x1c},
        {16384, 0x00, 0x20},  {32766, 0x00, 0x3c},  {32767, 0x00, 0x3c},  {-1, 0x7f, 0x48},   {-2048, 0x7f, 0x4c},
        {-16384, 0x7f, 0x68}, {-32767, 0x7f, 0x84}, {-32768, 0x7f, 0x84},
    };

    for (const Case &c : cases)
    {
        MasterTuneBytes got = encode_master_tune(c.tune);
        checks.add(check_fmt("tune %6d", c.tune), got.msb == c.msb && got.lsb == c.lsb,
                   check_fmt("%02x %02x, esperado %02x %02x", got.msb, got.lsb, c.msb, c.lsb));
    }

    // Monotonía por tramos: el LSB no puede retroceder al subir la afinación.
    int lastLsb = -1;
    int regressions = 0;
    for (int t = 0; t <= 32767; t += 97)
    {
        int lsb = encode_master_tune((int16_t)t).lsb;
        if (lsb < lastLsb)
            regressions++;
        lastLsb = lsb;
    }
    CHECK_EQ(regressions, 0);

    // Y nunca se sale del rango que el firmware acepta.
    int outOfRange = 0;
    for (int t = -32768; t <= 32767; t++)
    {
        MasterTuneBytes b = encode_master_tune((int16_t)t);
        if (b.msb != 0x00 && b.msb != 0x7f)
            outOfRange++;
        if (t >= 0 && b.lsb > 0x3c)
            outOfRange++;
        if (t < 0 && (b.lsb < 0x48 || b.lsb > 0x84))
            outOfRange++;
    }
    CHECK_EQ(outOfRange, 0);
}

// ---------------------------------------------------------------------------
// El arranque. Hasta la fase 1 el harness reimplementaba estos bytes a mano en
// vez de llamar al mismo sitio que el plugin, así que un arreglo del arranque
// dejaba el golden verde sin enterarse nadie. Esto fija la secuencia; que las
// dos capas la ejecuten es cosa de Mcu::boot().

TEST_SUITE(command_port_boot_sequence)
{
    CommandPort port;

    // Prólogo: parche 0 y afinación, antes de las 1024 muestras de margen.
    port.programChange(0);
    port.masterTune(0);
    check_bytes(checks, "arranque, antes del margen", drain(port), {0x30, 0xE0, 0x00, 0x00});

    // Epílogo: recarga del parche, después.
    port.reloadPatch();
    check_bytes(checks, "arranque, después del margen", drain(port), {0x31, 0x30});

    // Con afinación distinta de cero cambian dos bytes y solo dos.
    port.programChange(0);
    port.masterTune(-16384);
    check_bytes(checks, "arranque afinado", drain(port), {0x30, 0xE0, 0x7f, 0x68});
}

// ---------------------------------------------------------------------------
// Pánico. Antes de la fase 1 no existía y no había dónde ponerlo
// (FIABILIDAD §3). Esta suite es su especificación.

TEST_SUITE(command_port_all_notes_off)
{
    CommandPort port;
    port.allNotesOff();
    std::vector<u8> bytes = drain(port);

    // Pedal arriba primero: soltar las notas con el pedal pisado no apaga nada.
    CHECK_EQ(bytes.size(), 1 + 128 * 3);
    CHECK_EQ(bytes[0], 0x50);

    int badStatus = 0;
    int badVelocity = 0;
    std::vector<bool> seen(128, false);
    for (size_t i = 1; i + 2 < bytes.size() + 1; i += 3)
    {
        if (bytes[i] != 0xB0)
            badStatus++;
        if (bytes[i + 2] != 0x00)
            badVelocity++;
        if (bytes[i + 1] < 128)
            seen[bytes[i + 1]] = true;
    }

    CHECK_EQ(badStatus, 0);
    CHECK_EQ(badVelocity, 0);

    // Las 128, sin faltar ninguna.
    int missing = 0;
    for (int n = 0; n < 128; n++)
        if (!seen[n])
            missing++;
    CHECK_EQ(missing, 0);

    // Y cabe entero en el anillo: si no, el pánico se comería su propia cola.
    CHECK(1 + 128 * 3 <= (int)CommandQueue::CAPACITY);
    CHECK_EQ(port.queue().dropped(), 0);
}

// ---------------------------------------------------------------------------
// El anillo. Sustituye a un std::queue que reservaba memoria desde el hilo de
// audio (FIABILIDAD §12); lo que hay que fijar es que sea FIFO y que su
// comportamiento al desbordar esté decidido, no sea accidental.

TEST_SUITE(command_port_ring)
{
    CommandQueue q;

    CHECK(q.empty());
    CHECK_EQ(q.size(), 0);
    CHECK_EQ(q.dropped(), 0);

    // FIFO, en orden, dando varias vueltas al anillo para que el índice envuelva.
    int outOfOrder = 0;
    u8 next = 0;
    for (int round = 0; round < 5; round++)
    {
        for (int i = 0; i < 300; i++)
            q.push((u8)(next + i));
        for (int i = 0; i < 300; i++)
        {
            if (q.front() != (u8)(next + i))
                outOfOrder++;
            q.pop();
        }
        next = (u8)(next + 300);
    }
    CHECK_EQ(outOfOrder, 0);
    CHECK(q.empty());
    CHECK_EQ(q.dropped(), 0);

    // Al desbordar se descarta el byte NUEVO: lo que el firmware ya está
    // leyendo sigue siendo coherente, y el descarte se cuenta.
    size_t refused = 0;
    for (size_t i = 0; i < CommandQueue::CAPACITY; i++)
        if (!q.push((u8)i))
            refused++;
    CHECK_EQ(refused, 0);
    CHECK_EQ(q.size(), CommandQueue::CAPACITY);

    CHECK(!q.push(0xAA));
    CHECK_EQ(q.dropped(), 1);
    CHECK_EQ(q.size(), CommandQueue::CAPACITY);
    CHECK_EQ(q.front(), 0); // la cabeza no se ha movido

    // Y el 0xAA descartado no aparece luego por sorpresa.
    int ghosts = 0;
    for (size_t i = 0; i < CommandQueue::CAPACITY; i++)
    {
        if (q.front() != (u8)i)
            ghosts++;
        q.pop();
    }
    CHECK_EQ(ghosts, 0);
    CHECK(q.empty());

    // clear() deja el anillo utilizable, no solo vacío.
    q.push(1);
    q.push(2);
    q.clear();
    CHECK(q.empty());
    q.push(3);
    CHECK_EQ(q.front(), 3);

    // pop() sobre una cola vacía no puede desincronizar los índices.
    q.pop();
    q.pop();
    CHECK(q.empty());
    q.push(4);
    CHECK_EQ(q.size(), 1);
    CHECK_EQ(q.front(), 4);
}
