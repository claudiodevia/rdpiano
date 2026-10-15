#include "Lcd.h"
#include "lcd_font.h"
#include <JuceHeader.h>

/**
 * @file Lcd.cpp
 * @brief Dibujo del display: los caracteres van a una imagen cacheada, no al repintado.
 */

//==============================================================================
Lcd::Lcd() { memset(chars, ' ', kChars); }

Lcd::~Lcd() = default;

void Lcd::setText(const uint8_t (&text)[kChars])
{
    if (memcmp(chars, text, kChars) == 0)
        return;

    memcpy(chars, text, kChars);
    cache = juce::Image();
    repaint();
}

void Lcd::paint(juce::Graphics &g)
{
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

    const float unit = (float)getWidth() / kTextSpan;

    for (int i = 0; i < kRows; i++)
        for (int j = 0; j < kColumns; j++)
            drawGlyph((int)(i * (kLinePitch * unit)), (int)(j * (kPitch * unit)), chars[i * kColumns + j], unit, g);
}

static constexpr uint32_t lcd_col1 = 0xFF233336;
static constexpr uint32_t lcd_col2 = 0xFF73A5A9;

void Lcd::drawGlyph(int top, int left, uint8_t ch, float unit, juce::Graphics &g) const
{
    if (ch < kFirstGlyph)
        return;

    const uint8_t *f = lcd_font[ch - kFirstGlyph];
    for (int row = 0; row < 7; row++)
    {
        for (int col = 0; col < 5; col++)
        {
            g.setColour(juce::Colour((f[row] & (1 << (4 - col))) ? lcd_col1 : lcd_col2));
            g.fillRect(left + col * (kDotPitch * unit), top + row * (kDotPitch * unit), kDotSize * unit,
                       kDotSize * unit);
        }
    }
}
