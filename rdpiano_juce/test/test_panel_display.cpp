/**
 * @file test_panel_display.cpp
 * @brief El display del panel sin ventana: el texto de cada modo y los mapeos entre dial y valor.
 */

#include <JuceHeader.h>

#include <cstring>
#include <string>

#include "PanelDisplay.h"
#include "patches.h"
#include "unit_test.h"

namespace
{
    std::string row(const panel::DisplayFrame &frame, int r)
    {
        return std::string((const char *)frame.chars + r * Lcd::kColumns, Lcd::kColumns);
    }

    // Lo que enseñaba el display antes de sacar los nombres de `patchNames`, copiado a propósito: si el texto
    // del panel cambia, esta lista discrepa.
    const char *const kShownNames[NUM_PATCHES] = {
        "MKS-20           Piano 1          ", "MKS-20           Piano 2          ",
        "MKS-20           Piano 3          ", "MKS-20           Harpsichord      ",
        "MKS-20           Clavi            ", "MKS-20           Vibraphone       ",
        "MKS-20           E-Piano 1        ", "MKS-20           E-Piano 2        ",
        "MK-80            Classic          ", "MK-80            Special          ",
        "MK-80            Blend            ", "MK-80            Contemporary     ",
        "MK-80            A. Piano 1       ", "MK-80            A. Piano 2       ",
        "MK-80            Clavi            ", "MK-80            Vibraphone       "};
} // namespace

/**
 * @brief Modo parche: modelo arriba y nombre abajo, y el dial en el tramo del parche.
 */
TEST_SUITE(plugin_display_patch)
{
    for (int p = 0; p < NUM_PATCHES; p++)
    {
        const panel::DisplayFrame frame = panel::displayFrame(panel::kModePatch, p, 0, 0);
        const std::string shown((const char *)frame.chars, Lcd::kChars);
        checks.add(check_fmt("texto del parche %d", p), shown == kShownNames[p],
                   check_fmt("\"%s\", esperado \"%s\"", shown.c_str(), kShownNames[p]));
        checks.add(check_fmt("dial del parche %d", p), frame.dial == p / 16.0 * 2.0 - 1.0, check_fmt("%g", frame.dial));
        checks.add(check_fmt("el dial vuelve al parche %d", p), panel::patchFromDial(frame.dial) == p,
                   check_fmt("%d", panel::patchFromDial(frame.dial)));
    }

    // Los topes del dial caen en el primer y el último parche.
    CHECK_EQ(panel::patchFromDial(-1.0), 0);
    CHECK_EQ(panel::patchFromDial(1.0), NUM_PATCHES - 1);
}

/**
 * @brief Modo afinación: "TUNING" y los Hz con un decimal, 442 ± 3,85 Hz en los topes.
 */
TEST_SUITE(plugin_display_tune)
{
    struct Case
    {
        int tune;
        const char *hz;
    };
    const Case cases[] = {{0, "442.0Hz          "}, {32767, "445.9Hz          "}, {-32767, "438.1Hz          "}};

    for (const Case &c : cases)
    {
        const panel::DisplayFrame frame = panel::displayFrame(panel::kModeTune, 0, c.tune, 0);
        CHECK(row(frame, 0) == "TUNING           ");
        checks.add(check_fmt("Hz con afinación %d", c.tune), row(frame, 1) == c.hz,
                   check_fmt("\"%s\", esperado \"%s\"", row(frame, 1).c_str(), c.hz));
        CHECK(frame.dial == c.tune / 32767.0);
    }

    CHECK_EQ(panel::tuneFromDial(0.0), 0);
    CHECK_EQ(panel::tuneFromDial(1.0), 32767);
    CHECK_EQ(panel::tuneFromDial(-1.0), -32767);
}

/**
 * @brief Modos de parámetro: rótulo, número 1..15 a la derecha y la barra con el marcador 0xff en el paso.
 *        Y el dial: cada paso que enseña lo vuelve a elegir, y los topes caen en el primero y el último.
 */
TEST_SUITE(plugin_display_param)
{
    const panel::DisplayFrame frame = panel::displayFrame(panel::kModeChorusRate, 0, 0, 11);
    CHECK(row(frame, 0) == "CHORUS RATE    12");

    std::string bar(Lcd::kColumns, ' ');
    for (int i = 0; i < panel::kParamSteps; i++)
        bar[1 + i] = i == 11 ? (char)0xff : '_';
    CHECK(row(frame, 1) == bar);

    CHECK(row(panel::displayFrame(panel::kModePhaserDepth, 0, 0, 0), 0) == "PHASER DEPTH    1");

    for (int s = 0; s < panel::kParamSteps; s++)
    {
        const double dial = panel::displayFrame(panel::kModeTremoloDepth, 0, 0, s).dial;
        checks.add(check_fmt("el dial vuelve al paso %d", s), panel::stepFromDial(dial) == s,
                   check_fmt("dial %g da el paso %d", dial, panel::stepFromDial(dial)));
        checks.add(check_fmt("normalizado del paso %d", s),
                   panel::stepFromNormalised(panel::normalisedFromStep(s)) == s, "no vuelve");
    }

    CHECK_EQ(panel::stepFromDial(-1.0), 0);
    CHECK_EQ(panel::stepFromDial(1.0), panel::kParamSteps - 1);

    // Los quince pasos se reparten el recorrido por igual: ninguno queda solo en el tope.
    CHECK_EQ(panel::stepFromDial(0.95), panel::kParamSteps - 1);
}

/**
 * @brief La tabla de modos: los seis de parámetro tienen rótulo de 15 columnas y parámetro; parche y afinación,
 *        ninguno de los dos.
 */
TEST_SUITE(plugin_display_modes)
{
    for (int m = 0; m < panel::kNumDisplayModes; m++)
    {
        const panel::ModeSpec &spec = panel::modeSpecs[m];
        const bool editsParam = m != panel::kModePatch && m != panel::kModeTune;
        checks.add(check_fmt("modo %d", m),
                   editsParam ? spec.label != nullptr && std::strlen(spec.label) == 15 && spec.param < kNumRdParams
                              : spec.label == nullptr && spec.param == kNumRdParams,
                   "rótulo o parámetro inconsistentes");
    }
}
