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
/*
 */
class Lcd : public juce::Component
{
public:
  Lcd();
  ~Lcd() override;

  void paint(juce::Graphics &) override;

  // Dos filas de 17 caracteres, en el juego del display: no es texto de JUCE.
  // Antes esto recibía una `juce::String` y copiaba 34 bytes de su UTF-8, con
  // dos consecuencias (AUDITORIA §13): una cadena más corta leía fuera, y el
  // marcador de la barra de parámetros —el carácter 0xff— salía codificado
  // como dos bytes, así que ni se dibujaba ni dejaba el resto de la fila en su
  // sitio. Con un búfer de bytes del tamaño exacto ninguna de las dos cosas es
  // expresable.
  static constexpr int kColumns = 17;
  static constexpr int kRows = 2;
  static constexpr int kChars = kColumns * kRows;

  void setText(const uint8_t (&chars)[kChars]);

  void setScale(float scale);

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Lcd)

  void LCD_FontRenderStandard(int32_t x, int32_t y, uint8_t ch,
                              juce::Graphics &g);

  uint8_t LCD_Data[kChars];
  float scale = 1;
};
