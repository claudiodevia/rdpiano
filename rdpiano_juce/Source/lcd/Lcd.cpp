/*
  ==============================================================================

    Lcd.cpp
    Created: 14 Jan 2025 2:26:42am
    Author:  Giulio Zausa

  ==============================================================================
*/

#include "Lcd.h"
#include "lcd_font.h"
#include <JuceHeader.h>

//==============================================================================
Lcd::Lcd() { memset(LCD_Data, ' ', kChars); }

Lcd::~Lcd() = default;

void Lcd::setText(const uint8_t (&chars)[kChars])
{
    // Se repinta él, y sólo si el texto cambia: antes dependía del repaint()
    // de toda la ventana que hacía el editor, que reescalaba el fondo entero.
    if (memcmp(LCD_Data, chars, kChars) == 0)
        return;

    memcpy(LCD_Data, chars, kChars);
    cache = juce::Image();
    repaint();
}

void Lcd::setScale(float scale)
{
    if (scale == this->scale)
        return;

    this->scale = scale;
    cache = juce::Image();
}

void Lcd::paint(juce::Graphics &g)
{
    // Las dos filas son 1.190 rectangulillos de 5x7 píxeles por carácter: se
    // dibujan a una imagen y lo que hace el repintado es copiarla. La imagen se
    // rehace sólo cuando cambia el texto, la escala o el tamaño, y a la
    // resolución física del contexto para que en pantalla retina no se vea
    // interpolada.
    const float physical = (float)g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = juce::roundToInt(getWidth() * physical);
    const int h = juce::roundToInt(getHeight() * physical);

    if (w <= 0 || h <= 0)
        return;

    if (cache.isNull() || cache.getWidth() != w || cache.getHeight() != h)
        renderCache(w, h, physical);

    g.drawImageTransformed(cache, juce::AffineTransform::scale(1.0f / physical));
}

void Lcd::renderCache(int width, int height, float physical)
{
    cache = juce::Image(juce::Image::ARGB, width, height, true);

    juce::Graphics g(cache);
    g.addTransform(juce::AffineTransform::scale(physical));

    float sfC = 0.445 / (scale / 5);

    for (int i = 0; i < kRows; i++)
    {
        for (int j = 0; j < kColumns; j++)
        {
            uint8_t ch = LCD_Data[i * kColumns + j];
            LCD_FontRenderStandard(i * (50 * sfC), j * (34 * sfC), ch, g);
        }
    }
}

static constexpr uint32_t lcd_col1 = 0xFF233336;
static constexpr uint32_t lcd_col2 = 0xFF73A5A9;

// `top` y `left` son la esquina del carácter en píxeles, no fila y columna: se
// llamaban `x` e `y` y estaban cruzadas con las de `fillRect`.
void Lcd::LCD_FontRenderStandard(int32_t top, int32_t left, uint8_t ch, juce::Graphics &g)
{
    if (ch < 16)
        return;

    float sfC = 0.445 / (scale / 5);

    uint8_t *f = &lcd_font[ch - 16][0];
    for (int row = 0; row < 7; row++)
    {
        for (int col = 0; col < 5; col++)
        {
            g.setColour(juce::Colour((f[row] & (1 << (4 - col))) ? lcd_col1 : lcd_col2));
            g.fillRect(left + col * (6 * sfC), top + row * (6 * sfC), (5 * sfC), (5 * sfC));
        }
    }
}
