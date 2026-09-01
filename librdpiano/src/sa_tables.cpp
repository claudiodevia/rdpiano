#include "../include/sa_tables.h"

#include <cmath>

// Generación de las dos LUT de IC10/IC11, movida tal cual desde el constructor
// de SoundChip (REFACTORIZACION §4). No se ha tocado una sola operación: los
// dos `TODO: I want to believe there is a better way` siguen donde estaban, y
// test_sa_tables.cpp fija la salida byte a byte.

void sa_tables_generate(SaTables &out)
{

    // Exp table to for the subphase
    // TODO: This is bit accurate, but I want to believe there is a better way to compute this function
    for (size_t i = 0; i < 0x10000; i++)
    {
        // ROM IC11
        uint16_t r11_pos = i % 4096;
        uint16_t r11 = (uint16_t)round(exp2f(13.0 + r11_pos / 4096.0) - 4096 * 2);
        bool r11_12 = !((r11 >> 12) & 1);
        bool r11_11 = !((r11 >> 11) & 1);
        bool r11_10 = !((r11 >> 10) & 1);
        bool r11_9 = !((r11 >> 9) & 1);
        bool r11_8 = !((r11 >> 8) & 1);
        bool r11_7 = !((r11 >> 7) & 1);
        bool r11_6 = !((r11 >> 6) & 1);
        bool r11_5 = !((r11 >> 5) & 1);
        bool r11_4 = (r11 >> 4) & 1;
        bool r11_3 = (r11 >> 3) & 1;
        bool r11_2 = (r11 >> 2) & 1;
        bool r11_1 = (r11 >> 1) & 1;
        bool r11_0 = (r11 >> 0) & 1;

        uint8_t param_bus_0 = ((i / 0x1000) >> 0) & 1;
        uint8_t param_bus_1 = ((i / 0x1000) >> 1) & 1;
        uint8_t param_bus_2 = ((i / 0x1000) >> 2) & 1;
        uint8_t param_bus_3 = ((i / 0x1000) >> 3) & 1;

        // Copy pasted from silicon
        bool result_b0 = (!r11_6 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (!r11_5 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (r11_4 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (r11_3 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (r11_2 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (r11_1 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (r11_0 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3 && param_bus_0 && param_bus_1 &&
                          param_bus_2 && !param_bus_3);
        bool result_b1 = (!r11_7 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (!r11_6 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (!r11_5 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (r11_4 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                         (r11_3 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (r11_2 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (r11_1 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                         (r11_0 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3);
        bool result_b2 = !(!((!r11_8 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_7 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_6 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_5 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (r11_4 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_3 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_2 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_1 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !(r11_0 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b3 = !(!((!r11_9 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_8 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_7 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_6 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_5 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_4 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_3 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_2 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((r11_1 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_0 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b4 = !(!((!r11_10 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_9 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_8 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_7 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_6 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_5 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_4 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_3 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((r11_2 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_1 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_0 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (0 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b5 = !(!((!r11_11 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_10 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_9 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_8 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_7 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_6 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_5 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (r11_4 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((r11_3 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_2 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_1 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_0 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b6 = !(!((!r11_12 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_11 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_10 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_9 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_8 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_7 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_6 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_5 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((r11_4 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_3 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_2 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_1 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b7 = !(!((1 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_12 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_11 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_10 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_9 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_8 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_7 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_6 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((!r11_5 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_4 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_3 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_2 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b8 = !(!((0 && !param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (1 && param_bus_0 && !param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_12 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_11 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_10 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_9 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_8 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_7 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3)) &&
                           !((!r11_6 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (!r11_5 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_4 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_3 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b9 = !(!((1 && !param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_12 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                             (!r11_11 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_10 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_9 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_8 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                             (!r11_7 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (!r11_6 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3)) &&
                           !((!r11_5 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                             (r11_4 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)));
        bool result_b10 = !(!((1 && param_bus_0 && param_bus_1 && !param_bus_2 && !param_bus_3) ||
                              (!r11_12 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                              (!r11_11 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                              (!r11_10 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                              (!r11_9 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                              (!r11_8 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                              (!r11_7 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                              (!r11_6 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3)) &&
                            !(!r11_5 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b11 = (1 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_12 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_11 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_10 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_9 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_8 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_7 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_6 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3);
        bool result_b12 = (0 && !param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (1 && param_bus_0 && !param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_12 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_11 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_10 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_9 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_8 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_7 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3);
        bool result_b13 = (1 && !param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_12 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) ||
                          (!r11_11 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_10 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_9 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) ||
                          (!r11_8 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3);
        bool result_b14 = !(1 && !(1 && param_bus_0 && param_bus_1 && param_bus_2 && !param_bus_3) &&
                            !(!r11_12 && !param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_11 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_10 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_9 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b15 = !(!(!param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_12 && param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_11 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_10 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b16 = !(!(param_bus_0 && !param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_12 && !param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_11 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b17 = !(!(!param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3) &&
                            !(!r11_12 && param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3));
        bool result_b18 = param_bus_0 && param_bus_1 && !param_bus_2 && param_bus_3;

        uint32_t result = result_b18 << 18 | result_b17 << 17 | result_b16 << 16 | result_b15 << 15 | result_b14 << 14 |
                          result_b13 << 13 | result_b12 << 12 | result_b11 << 11 | result_b10 << 10 | result_b9 << 9 |
                          result_b8 << 8 | result_b7 << 7 | result_b6 << 6 | result_b5 << 5 | result_b4 << 4 |
                          result_b3 << 3 | result_b2 << 2 | result_b1 << 1 | result_b0 << 0;
        out.phase_exp[i] = result;
    }

    // Exp table to decode samples
    // TODO: This is bit accurate, but I want to believe there is a better way to compute this function
    for (size_t i = 0; i < 0x8000; i++)
    {
        // ROM IC10
        uint16_t r10_pos = i % 1024;
        uint16_t r10 = (uint16_t)round(exp2f(11.0 + ~r10_pos / 1024.0) - 1024);
        bool r10_9 = (r10 >> 0) & 1;
        bool r10_8 = (r10 >> 1) & 1;
        bool r10_0 = (r10 >> 2) & 1;
        bool r10_1 = (r10 >> 3) & 1;
        bool r10_2 = (r10 >> 4) & 1;
        bool r10_3 = !((r10 >> 5) & 1);
        bool r10_4 = !((r10 >> 6) & 1);
        bool r10_5 = !((r10 >> 7) & 1);
        bool r10_6 = !((r10 >> 8) & 1);
        bool r10_7 = !((r10 >> 9) & 1);

        bool wavein_sign = i >= 0x4000;
        uint8_t add_r_0 = ((i / 0x400) >> 0) & 1;
        uint8_t add_r_1 = ((i / 0x400) >> 1) & 1;
        uint8_t add_r_2 = ((i / 0x400) >> 2) & 1;
        uint8_t add_r_3 = ((i / 0x400) >> 3) & 1;

        // Copy pasted from silicon
        bool result_b14 = !((!(!add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) && !wavein_sign) ||
                            (!add_r_3 && !add_r_2 && !add_r_1 && !add_r_0 && wavein_sign));
        bool result_b13 = !((((!r10_7 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                             wavein_sign) ||
                            (!((!r10_7 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                               (!add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                             !wavein_sign));
        bool result_b12 = !((((!r10_6 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_7 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (!add_r_3 && !add_r_2 && add_r_1 && !add_r_0)) &&
                             wavein_sign) ||
                            (!((!r10_6 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                               (!r10_7 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                               (!add_r_3 && !add_r_2 && add_r_1 && !add_r_0)) &&
                             !wavein_sign));
        bool result_b11 = !((((!r10_5 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_6 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_7 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (1 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0)) &&
                             wavein_sign) ||
                            (!((!r10_5 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                               (!r10_6 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                               (!r10_7 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                               (1 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0)) &&
                             !wavein_sign));
        bool result_b10 = !((!((!r10_7 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                               (!r10_6 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                               (!r10_5 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                               (!r10_4 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                             !(!add_r_3 && add_r_2 && !add_r_1 && !add_r_0) && !wavein_sign) ||
                            (!(!((!r10_7 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                                 (!r10_6 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                                 (!r10_5 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                                 (!r10_4 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                               !(!add_r_3 && add_r_2 && !add_r_1 && !add_r_0)) &&
                             wavein_sign));
        bool result_b9 = !((((1 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                             (!r10_7 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                             (!r10_6 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                             (!r10_5 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                             (!r10_4 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                             (!r10_3 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                            wavein_sign) ||
                           (!((1 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_7 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_6 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_5 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_4 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_3 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                            !wavein_sign));
        bool result_b8 = !((((1 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                             (!r10_7 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                             (!r10_6 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                             (!r10_5 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                             (!r10_4 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                             (!r10_3 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                             (r10_2 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) || (1 && 0)) &&
                            wavein_sign) ||
                           (!((1 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_7 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_6 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_5 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_4 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_3 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (r10_2 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) || (1 && 0)) &&
                            !wavein_sign));
        bool result_b7 = !((((1 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                             (!r10_7 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                             (!r10_6 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                             (!r10_5 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                             (!r10_4 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                             (!r10_3 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                             (r10_2 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                             (r10_1 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                            wavein_sign) ||
                           (!((1 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_7 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_6 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_5 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_4 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_3 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (r10_2 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (r10_1 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                            !wavein_sign));
        bool result_b6 = !((!((1 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_7 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_6 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_5 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_4 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_3 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (r10_2 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (r10_1 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                            !(r10_0 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) && !wavein_sign) ||
                           (!(!((1 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                                (!r10_7 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                                (!r10_6 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                                (!r10_5 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                                (!r10_4 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                                (!r10_3 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                                (r10_2 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                                (r10_1 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                              !(r10_0 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
                            wavein_sign));
        bool result_b5 = !((!((!r10_7 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_6 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                              (!r10_5 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_4 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_3 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (r10_2 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (r10_1 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (r10_0 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                            !((r10_9 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                            !wavein_sign) ||
                           (!(!((!r10_7 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                                (!r10_6 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                                (!r10_5 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                                (!r10_4 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                                (!r10_3 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                                (r10_2 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                                (r10_1 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                                (r10_0 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
                              !((r10_9 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                                (add_r_3 && !add_r_2 && !add_r_1 && add_r_0))) &&
                            wavein_sign));
        bool result_b4 = !((!((r10_8 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (r10_9 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (r10_0 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                              (r10_1 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                              (r10_2 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_3 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                              (!r10_4 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                              (!r10_5 && !add_r_3 && add_r_2 && add_r_1 && add_r_0)) &&
                            !((!r10_6 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                              (!r10_7 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                              (add_r_3 && !add_r_2 && add_r_1 && !add_r_0)) &&
                            !wavein_sign) ||
                           (!(!((r10_8 && !add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                                (r10_9 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                                (r10_0 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                                (r10_1 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                                (r10_2 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                                (!r10_3 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                                (!r10_4 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                                (!r10_5 && !add_r_3 && add_r_2 && add_r_1 && add_r_0)) &&
                              !((!r10_6 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                                (!r10_7 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                                (add_r_3 && !add_r_2 && add_r_1 && !add_r_0))) &&
                            wavein_sign));
        bool result_b3 = !(
            (!((r10_8 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
               (r10_9 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
               (r10_0 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
               (r10_1 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
               (r10_2 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
               (!r10_3 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
               (!r10_4 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
               (!r10_5 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
             !((!r10_6 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
               (!r10_7 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) || (add_r_3 && !add_r_2 && add_r_1 && add_r_0)) &&
             !wavein_sign) ||
            (!(!((r10_8 && !add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                 (r10_9 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                 (r10_0 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                 (r10_1 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (r10_2 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                 (!r10_3 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                 (!r10_4 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                 (!r10_5 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0)) &&
               !((!r10_6 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                 (!r10_7 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                 (add_r_3 && !add_r_2 && add_r_1 && add_r_0))) &&
             wavein_sign));
        bool result_b2 = !(
            (!((r10_8 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
               (r10_9 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
               (r10_0 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
               (r10_1 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
               (r10_2 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
               (!r10_3 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
               (!r10_4 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
               (!r10_5 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
             !((!r10_6 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
               (!r10_7 && add_r_3 && !add_r_2 && add_r_1 && add_r_0) || (add_r_3 && add_r_2 && !add_r_1 && !add_r_0)) &&
             !wavein_sign) ||
            (!(!((r10_8 && !add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                 (r10_9 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                 (r10_0 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (r10_1 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                 (r10_2 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                 (!r10_3 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                 (!r10_4 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                 (!r10_5 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0)) &&
               !((!r10_6 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                 (!r10_7 && add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                 (add_r_3 && add_r_2 && !add_r_1 && !add_r_0))) &&
             wavein_sign));
        bool result_b1 = !(
            (!((r10_8 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
               (r10_9 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
               (r10_0 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
               (r10_1 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
               (r10_2 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
               (!r10_3 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
               (!r10_4 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
               (!r10_5 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0)) &&
             !((!r10_6 && add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
               (!r10_7 && add_r_3 && add_r_2 && !add_r_1 && !add_r_0) || (add_r_3 && add_r_2 && !add_r_1 && add_r_0)) &&
             !wavein_sign) ||
            (!(!((r10_8 && !add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                 (r10_9 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (r10_0 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                 (r10_1 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                 (r10_2 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                 (!r10_3 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                 (!r10_4 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                 (!r10_5 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0)) &&
               !((!r10_6 && add_r_3 && !add_r_2 && add_r_1 && add_r_0) ||
                 (!r10_7 && add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (add_r_3 && add_r_2 && !add_r_1 && add_r_0))) &&
             wavein_sign));
        bool result_b0 = !(
            (!((r10_8 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
               (r10_9 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
               (r10_0 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
               (r10_1 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
               (r10_2 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
               (!r10_3 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
               (!r10_4 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
               (!r10_5 && add_r_3 && !add_r_2 && add_r_1 && add_r_0)) &&
             !((!r10_6 && add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
               (!r10_7 && add_r_3 && add_r_2 && !add_r_1 && add_r_0) || (add_r_3 && add_r_2 && add_r_1 && !add_r_0)) &&
             !wavein_sign) ||
            (!(!((r10_8 && !add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (r10_9 && !add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                 (r10_0 && !add_r_3 && add_r_2 && add_r_1 && !add_r_0) ||
                 (r10_1 && !add_r_3 && add_r_2 && add_r_1 && add_r_0) ||
                 (r10_2 && add_r_3 && !add_r_2 && !add_r_1 && !add_r_0) ||
                 (!r10_3 && add_r_3 && !add_r_2 && !add_r_1 && add_r_0) ||
                 (!r10_4 && add_r_3 && !add_r_2 && add_r_1 && !add_r_0) ||
                 (!r10_5 && add_r_3 && !add_r_2 && add_r_1 && add_r_0)) &&
               !((!r10_6 && add_r_3 && add_r_2 && !add_r_1 && !add_r_0) ||
                 (!r10_7 && add_r_3 && add_r_2 && !add_r_1 && add_r_0) ||
                 (add_r_3 && add_r_2 && add_r_1 && !add_r_0))) &&
             wavein_sign));

        uint16_t result = result_b14 << 14 | result_b13 << 13 | result_b12 << 12 | result_b11 << 11 | result_b10 << 10 |
                          result_b9 << 9 | result_b8 << 8 | result_b7 << 7 | result_b6 << 6 | result_b5 << 5 |
                          result_b4 << 4 | result_b3 << 3 | result_b2 << 2 | result_b1 << 1 | result_b0 << 0;
        out.samples_exp[i] = result;
    }
}

const SaTables &sa_tables()
{
    static const SaTables *tables = [] {
        SaTables *t = new SaTables();
        sa_tables_generate(*t);
        return t;
    }();
    return *tables;
}
