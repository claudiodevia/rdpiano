#ifndef RD_HELD_NOTES_H
#define RD_HELD_NOTES_H

/**
 * @file held_notes.h
 * @brief El espejo de lo que el firmware cree pulsado, para devolvérselo tras un cambio que lo apaga.
 */

#include "mame_utils.h"
#include "midi.h"

class Mcu;

/**
 * @brief Velocidad de cada tecla pulsada (0 = suelta) y el pedal, tal como los recibió el firmware.
 *
 * El program change de un cambio de parche y el switcharoo de la afinación apagan las voces y sueltan el
 * pedal dentro del firmware; esto es lo que se le devuelve después.
 */
class HeldNotes
{
  public:
    /**
     * @brief Apunta una tecla o el pedal.
     * @return true si es un note-on: empieza un ataque.
     */
    bool track(const KeyEvent &key);

    /** @brief Si el firmware cree pulsada la tecla. */
    bool isHeld(u8 note) const { return velocity[note & 0x7f] != 0; }

    /** @brief Todo suelto: lo que deja boot() o un pánico. */
    void clear();

    /**
     * @brief Le devuelve al firmware el pedal y luego las teclas pulsadas; lo sostenido solo por el pedal no.
     *
     * El pedal va primero: al revés, las notas nacerían sin sostenido. Solo encola bytes, así que vale en el
     * hilo de audio.
     *
     * @param velocityDrop Lo que se resta a cada velocidad para reentrar al nivel alcanzado; el suelo es 1.
     * @return Cuántas notas han reentrado.
     */
    int restore(Mcu &mcu, int velocityDrop) const;

  private:
    u8 velocity[128] = {0};
    bool sustainDown = false;
};

#endif
