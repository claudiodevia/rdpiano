/*
  ==============================================================================

    Lcd.h
    Created: 14 Jan 2025 2:26:42am
    Author:  Giulio Zausa

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
// El display de dos filas del panel, con el juego de caracteres del original.
class Lcd : public juce::Component
{
  public:
    Lcd();
    ~Lcd() override;

    void paint(juce::Graphics &) override;

    // Dos filas de 17 caracteres, en el juego del display: bytes, no texto de
    // JUCE. Con una `juce::String` el marcador 0xff de la barra de parámetros
    // salía en UTF-8 de dos bytes y descuadraba la fila.
    static constexpr int kColumns = 17;
    static constexpr int kRows = 2;
    static constexpr int kChars = kColumns * kRows;

    void setText(const uint8_t (&chars)[kChars]);

    void setScale(float scale);

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Lcd)

    void LCD_FontRenderStandard(int32_t top, int32_t left, uint8_t ch, juce::Graphics &g);
    void renderCache(int width, int height, float physical);

    uint8_t LCD_Data[kChars];
    float scale = 1;

    // El display ya dibujado: se rehace al cambiar el texto o la escala, no en
    // cada repintado.
    juce::Image cache;
};
