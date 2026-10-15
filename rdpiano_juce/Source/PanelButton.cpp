/**
 * @file PanelButton.cpp
 * @brief Dibujo del botón del panel.
 */

#include "PanelButton.h"

void PanelButton::setArt(juce::Rectangle<int> area, float newScale)
{
    artArea = area;
    scale = newScale;
}

void PanelButton::setLit(bool shouldBeLit)
{
    if (lit == shouldBeLit)
        return;

    lit = shouldBeLit;
    repaint();
}

void PanelButton::paintButton(juce::Graphics &g, bool, bool isButtonDown)
{
    const auto topLeft = getBoundsInParent().getTopLeft();
    const float shift = isButtonDown ? kPressShift / scale : 0;

    g.drawImage(art, artArea.getX() / scale - topLeft.x + shift, artArea.getY() / scale - topLeft.y - shift,
                artArea.getWidth() / scale, artArea.getHeight() / scale, artArea.getX(), artArea.getY(),
                artArea.getWidth(), artArea.getHeight());

    if (lit)
    {
        g.setColour(juce::Colours::red);
        g.fillRect(kLampArea.getX() / scale + shift, kLampArea.getY() / scale - shift, kLampArea.getWidth() / scale,
                   kLampArea.getHeight() / scale);
    }
}
