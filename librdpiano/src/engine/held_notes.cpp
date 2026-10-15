#include "held_notes.h"

#include "mcu.h"

/**
 * @file held_notes.cpp
 * @brief El espejo de teclas y pedal, y su devolución al firmware.
 */

bool HeldNotes::track(const KeyEvent &key)
{
    switch (key.kind)
    {
    case KeyEvent::NoteOn:
        velocity[key.note & 0x7f] = (u8)(key.velocity & 0x7f);
        return true;
    case KeyEvent::NoteOff:
        velocity[key.note & 0x7f] = 0;
        return false;
    case KeyEvent::Sustain:
        sustainDown = key.pedalDown;
        return false;
    case KeyEvent::Other:
        return false;
    }
    return false;
}

void HeldNotes::clear()
{
    for (u8 &v : velocity)
        v = 0;
    sustainDown = false;
}

int HeldNotes::restore(Mcu &mcu, int velocityDrop) const
{
    if (sustainDown)
        mcu.sustain(true);

    int retriggered = 0;
    for (int note = 0; note < 128; note++)
    {
        if (velocity[note] == 0)
            continue;

        int v = (int)velocity[note] - velocityDrop;
        if (v < 1)
            v = 1;

        mcu.noteOn((u8)note, (u8)v);
        retriggered++;
    }
    return retriggered;
}
