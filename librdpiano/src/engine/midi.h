#ifndef RD_ENGINE_MIDI_H
#define RD_ENGINE_MIDI_H

/**
 * @file midi.h
 * @brief Lo que el motor sabe del MIDI: la cola de un bloque y qué entiende el firmware de cada mensaje.
 */

#include "mame_utils.h"

inline constexpr u8 kMidiNoteOff = 0x8;          ///< Nibble alto del estado de un note-off.
inline constexpr u8 kMidiNoteOn = 0x9;           ///< Nibble alto del estado de un note-on.
inline constexpr u8 kMidiControlChange = 0xb;    ///< Nibble alto del estado de un control change.
inline constexpr u8 kMidiProgramChange = 0xc;    ///< Nibble alto del estado de un program change.
inline constexpr u8 kMidiSustainController = 64; ///< Número de controlador del pedal de sostenido.
inline constexpr u8 kMidiSustainDownFrom = 64;   ///< Valor del pedal desde el que está abajo.

/** @brief Un evento MIDI con su posición dentro del bloque, en muestras del host. */
struct MidiEvent
{
    int frame = 0; ///< Posición dentro del bloque.
    u8 status = 0; ///< Byte de estado.
    u8 data1 = 0;  ///< Primer byte de datos.
    u8 data2 = 0;  ///< Segundo byte de datos.
};

/** @brief La cola de un bloque: tamaño fijo, sin reservas. La llena pushMidi() y la vacía render(). */
class MidiQueue
{
  public:
    static constexpr int kCapacity = 512; ///< Eventos por bloque.

    /** @brief Encola un evento; false si la cola está llena y se pierde. */
    bool push(int frame, u8 status, u8 data1, u8 data2)
    {
        if (count >= kCapacity)
            return false;
        events[count++] = {frame, status, data1, data2};
        return true;
    }

    void clear() { count = 0; }
    int size() const { return count; }
    const MidiEvent &operator[](int i) const { return events[i]; }

  private:
    MidiEvent events[kCapacity];
    int count = 0;
};

/** @brief Lo que el firmware entiende de un mensaje de canal: una tecla, el pedal o nada. */
struct KeyEvent
{
    /** @brief Qué es. */
    enum Kind
    {
        Other,   ///< Algo que el firmware no atiende.
        NoteOn,  ///< Note-on con velocidad > 0.
        NoteOff, ///< Note-off, o note-on con velocidad 0.
        Sustain, ///< El CC del pedal de sostenido.
    };

    Kind kind = Other;      ///< Qué es.
    u8 note = 0;            ///< La tecla: el primer byte de datos tal como llegó.
    u8 velocity = 0;        ///< La velocidad de un NoteOn.
    bool pedalDown = false; ///< El pedal de un Sustain.

    /** @brief Clasifica un mensaje MIDI como lo hace Mcu::sendMidiCmd(), salvo el program change (Other). */
    static KeyEvent decode(u8 status, u8 data1, u8 data2)
    {
        KeyEvent key;
        const u8 command = (u8)(status >> 4);
        key.note = data1;
        if (command == kMidiNoteOn && data2 > 0)
        {
            key.kind = NoteOn;
            key.velocity = data2;
        }
        else if (command == kMidiNoteOff || command == kMidiNoteOn)
            key.kind = NoteOff;
        else if (command == kMidiControlChange && data1 == kMidiSustainController)
        {
            key.kind = Sustain;
            key.pedalDown = data2 >= kMidiSustainDownFrom;
        }
        return key;
    }

    /** @brief El note-off de una tecla. */
    static KeyEvent release(u8 note)
    {
        KeyEvent key;
        key.kind = NoteOff;
        key.note = note;
        return key;
    }
};

#endif
