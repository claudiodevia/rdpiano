/**
 * @file PanelLookAndFeel.cpp
 * @brief Dibujo del dial alfa y del tirador del fader.
 */

#include "PanelLookAndFeel.h"
#include "PanelLayout.h"

void AlphaDialLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float sliderPos,
                                            const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider &)
{
    const double scale = panel::kDialArea.getWidth() / width;
    const float centerX = x + width * 0.5f;
    const float centerY = y + height * 0.5f;
    const double size = kSpriteSize / scale;
    const double radius = juce::jmin(width * 0.5f, height * 0.5f) - size / 2 - kSpriteInset / scale;

    // El ángulo 0 de JUCE apunta arriba y el del seno y el coseno, a la derecha.
    const double angle =
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle) + juce::MathConstants<double>::pi * 1.5;
    const double px = centerX + std::cos(angle) * radius;
    const double py = centerY + std::sin(angle) * radius;

    g.drawImage(sprite, (int)(px - size / 2), (int)(py - size / 2), (int)size, (int)size, 0, 0, kSpriteSize,
                kSpriteSize);
}

void FaderLookAndFeel::drawLinearSlider(juce::Graphics &g, int, int, int, int, float, float, float,
                                        juce::Slider::SliderStyle, juce::Slider &slider)
{
    const juce::Rectangle<int> &track = panel::kFaderTrack;
    const juce::Rectangle<int> &cap = panel::kFaderCapArt;
    const float scale = (float)track.getWidth() / slider.getWidth();
    const float value = (float)slider.valueToProportionOfLength(slider.getValue());

    g.drawImage(sheet, 0, (int)((1 - value) * (track.getHeight() - cap.getHeight()) / scale),
                (int)(cap.getWidth() / scale), (int)(cap.getHeight() / scale), cap.getX(), cap.getY(), cap.getWidth(),
                cap.getHeight());
}
