#pragma once

#include <JuceHeader.h>

/**
 * @file Lcd.h
 * @brief El display de dos filas del panel, con el juego de caracteres del original.
 */

/**
 * @brief El display: 2×17 caracteres de 5×7 puntos, dibujados a una imagen cacheada.
 *
 * El tamaño de los puntos sale del ancho del componente: los 17 caracteres lo llenan de borde a borde.
 */
class Lcd : public juce::Component
{
  public:
    Lcd();
    ~Lcd() override;

    void paint(juce::Graphics &) override;

    static constexpr int kColumns = 17;             ///< Caracteres por fila.
    static constexpr int kRows = 2;                 ///< Filas.
    static constexpr int kChars = kColumns * kRows; ///< Las dos filas seguidas.

    /**
     * @brief Cambia lo que se lee en el display.
     * @param text Las dos filas seguidas, en el juego de caracteres del original: bytes y no `juce::String`,
     *             que codificaría en UTF-8 los caracteres por encima de 0x7f.
     */
    void setText(const uint8_t (&text)[kChars]);

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Lcd)

    static constexpr int kPitch = 34;                            ///< De un carácter al siguiente, en unidades de celda.
    static constexpr int kLinePitch = 50;                        ///< De una fila a la siguiente, en unidades de celda.
    static constexpr int kDotPitch = 6;                          ///< De un punto al siguiente.
    static constexpr int kDotSize = 5;                           ///< Lado de un punto.
    static constexpr int kGlyphWidth = 4 * kDotPitch + kDotSize; ///< Cinco puntos.

    /// Lo que ocupan a lo ancho los 17 caracteres, de la izquierda del primero a la derecha del último: el
    /// ancho del componente dividido por esto es la unidad de celda.
    static constexpr int kTextSpan = (kColumns - 1) * kPitch + kGlyphWidth;

    void drawGlyph(int top, int left, uint8_t ch, float unit, juce::Graphics &g) const;
    void renderCache(int width, int height, float physical);

    uint8_t chars[kChars];

    /// El display ya dibujado (34 caracteres son 1.190 puntos), a la resolución física del contexto para que
    /// en retina no se vea interpolado. Se rehace al cambiar el texto o el tamaño; el repintado solo lo copia.
    juce::Image cache;
};
