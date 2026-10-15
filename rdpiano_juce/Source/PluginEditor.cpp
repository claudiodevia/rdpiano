#include "PluginEditor.h"
#include "PluginProcessor.h"

/**
 * @file PluginEditor.cpp
 * @brief La tabla de botones del panel y los bucles que la recorren.
 */

typedef RdPiano_juceAudioProcessorEditor Editor;

/// Los 17 botones, en coordenadas del fondo. Es la única copia: el constructor,
/// resized(), pressButton() y updateValues() recorren esta tabla.
const Editor::ButtonSpec Editor::buttonSpecs[Editor::kNumButtons] = {
    // MKS-20 / MK-80: los dos bancos de ocho parches
    {.bounds = {2602, 806, 248, 248}, .art = {2598, 800, 257, 258}, .action = ButtonSpec::kSelectBank, .value = 0},
    {.bounds = {2602, 1068, 248, 248},
     .art = {2598, 1058, 257, 260},
     .action = ButtonSpec::kSelectBank,
     .value = kPatchesPerBank},

    // Los ocho botones de parche, dentro del banco que esté activo
    {.bounds = {1504, 806, 250, 248}, .art = {1478, 800, 290, 260}, .action = ButtonSpec::kSelectPatch, .value = 0},
    {.bounds = {1764, 806, 248, 248}, .art = {1759, 800, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 1},
    {.bounds = {2024, 806, 248, 248}, .art = {2018, 800, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 2},
    {.bounds = {2284, 806, 248, 248}, .art = {2279, 800, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 3},
    {.bounds = {1504, 1068, 250, 248}, .art = {1478, 1058, 290, 260}, .action = ButtonSpec::kSelectPatch, .value = 4},
    {.bounds = {1764, 1068, 248, 248}, .art = {1759, 1058, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 5},
    {.bounds = {2024, 1068, 248, 248}, .art = {2018, 1058, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 6},
    {.bounds = {2284, 1068, 248, 248}, .art = {2279, 1058, 262, 260}, .action = ButtonSpec::kSelectPatch, .value = 7},

    // TUNE: un único modo, así que entra y sale del mismo (first == second)
    {.bounds = {669, 295, 273, 273},
     .art = {669, 295, 273, 273},
     .action = ButtonSpec::kCycleModes,
     .first = panel::kModeTune,
     .second = panel::kModeTune},

    // Chorus, trémolo y EFX: on/off arriba, rate/depth abajo
    {.bounds = {2917, 806, 248, 248},
     .art = {2917, 800, 262, 262},
     .action = ButtonSpec::kToggleParam,
     .param = kChorusEnabled},
    {.bounds = {2917, 1068, 248, 248},
     .art = {2917, 1058, 262, 262},
     .action = ButtonSpec::kCycleModes,
     .first = panel::kModeChorusRate,
     .second = panel::kModeChorusDepth},
    {.bounds = {3229, 806, 248, 248},
     .art = {3229, 800, 262, 262},
     .action = ButtonSpec::kToggleParam,
     .param = kTremoloEnabled},
    {.bounds = {3229, 1068, 248, 248},
     .art = {3229, 1058, 262, 262},
     .action = ButtonSpec::kCycleModes,
     .first = panel::kModeTremoloRate,
     .second = panel::kModeTremoloDepth},
    {.bounds = {3547, 806, 248, 248},
     .art = {3547, 800, 262, 262},
     .action = ButtonSpec::kToggleParam,
     .param = kEfxEnabled},
    {.bounds = {3547, 1068, 248, 248},
     .art = {3547, 1058, 262, 262},
     .action = ButtonSpec::kCycleModes,
     .first = panel::kModePhaserRate,
     .second = panel::kModePhaserDepth},
};

//==============================================================================
RdPiano_juceAudioProcessorEditor::RdPiano_juceAudioProcessorEditor(RdPiano_juceAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p), volumeAttachment(p.param(kVolume), volumeSlider)
{
    addAndMakeVisible(lcd);

    backgroundArt = juce::ImageCache::getFromMemory(BinaryData::background_png, BinaryData::background_pngSize);
    interactableArt = juce::ImageCache::getFromMemory(BinaryData::interactable_png, BinaryData::interactable_pngSize);
    dialLookAndFeel.setSprite(
        juce::ImageCache::getFromMemory(BinaryData::alphadial_png, BinaryData::alphadial_pngSize));
    faderLookAndFeel.setSheet(interactableArt);

    for (int i = 0; i < kNumButtons; i++)
    {
        buttons[i].setSheet(interactableArt);
        buttons[i].onClick = [this, i] { pressButton(i); };
        addAndMakeVisible(buttons[i]);
    }

    addAndMakeVisible(alphaDial);
    alphaDial.setLookAndFeel(&dialLookAndFeel);
    alphaDial.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    alphaDial.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    alphaDial.setRange(-1, 1);
    alphaDial.addListener(this);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setLookAndFeel(&faderLookAndFeel);
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);

    setResizeLimits(panel::kUiWidth, panel::kUiHeight, panel::kUiWidth, panel::kUiHeight);
    setSize(panel::kUiWidth, panel::kUiHeight);

    updateValues();
    p.addChangeListener(this);
}

RdPiano_juceAudioProcessorEditor::~RdPiano_juceAudioProcessorEditor()
{
    alphaDial.setLookAndFeel(nullptr);
    volumeSlider.setLookAndFeel(nullptr);
    audioProcessor.removeChangeListener(this);
}

float RdPiano_juceAudioProcessorEditor::artScale() const { return (float)panel::kArtWidth / getWidth(); }

void RdPiano_juceAudioProcessorEditor::resized()
{
    const float scale = artScale();

    lcd.setBounds(panel::toUi(panel::kLcdArea, scale));

    for (int i = 0; i < kNumButtons; i++)
    {
        const ButtonSpec &spec = buttonSpecs[i];
        buttons[i].setBounds(panel::toUi(spec.bounds, scale));
        buttons[i].setArt(spec.art, scale);
    }

    alphaDial.setBounds(panel::toUi(panel::kDialArea, scale));
    volumeSlider.setBounds(panel::toUi(panel::kFaderTrack, scale));
}

void RdPiano_juceAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.drawImage(backgroundArt, getLocalBounds().toFloat());
}

void RdPiano_juceAudioProcessorEditor::pressButton(int index)
{
    const ButtonSpec &spec = buttonSpecs[index];
    const panel::DisplayMode previous = mode;

    mode = panel::kModePatch;

    switch (spec.action)
    {
    case ButtonSpec::kSelectBank:
        audioProcessor.setCurrentProgram(spec.value);
        break;

    case ButtonSpec::kSelectPatch:
        audioProcessor.setCurrentProgram(spec.value +
                                         (audioProcessor.getCurrentProgram() >= kPatchesPerBank ? kPatchesPerBank : 0));
        break;

    case ButtonSpec::kToggleParam:
        audioProcessor.setParamValue(spec.param, audioProcessor.paramValue(spec.param) >= 0.5f ? 0.0f : 1.0f);
        updateValues();
        break;

    case ButtonSpec::kCycleModes:
        if (previous != spec.first && previous != spec.second)
            mode = spec.first;
        else if (previous == spec.first && spec.second != spec.first)
            mode = spec.second;
        updateValues();
        break;
    }
}

int RdPiano_juceAudioProcessorEditor::paramStep(panel::DisplayMode m) const
{
    const RdParamId id = panel::modeSpecs[m].param;
    return panel::stepFromNormalised(audioProcessor.param(id).convertTo0to1(audioProcessor.paramValue(id)));
}

void RdPiano_juceAudioProcessorEditor::sliderValueChanged(juce::Slider *slider)
{
    if (slider != &alphaDial)
        return;

    const double value = alphaDial.getValue(); // -1..1

    if (mode == panel::kModePatch)
    {
        const int patch = panel::patchFromDial(value);
        if (dialDragging)
        {
            dialPreview = patch;
            updateValues();
            return;
        }

        audioProcessor.setCurrentProgram(patch);
        return;
    }

    if (mode == panel::kModeTune)
    {
        const int tune = panel::tuneFromDial(value);
        if (dialDragging)
        {
            dialPreview = tune;
            updateValues();
            return;
        }

        audioProcessor.setMasterTune((int16_t)tune);
        return;
    }

    setParamStep(panel::modeSpecs[mode].param, panel::stepFromDial(value));
    updateValues();
}

void RdPiano_juceAudioProcessorEditor::setParamStep(RdParamId id, int step)
{
    juce::RangedAudioParameter &param = audioProcessor.param(id);
    const float value = param.convertFrom0to1(panel::normalisedFromStep(step));

    if (dialGesture != id)
    {
        audioProcessor.setParamValue(id, value);
        return;
    }

    const float normalised = param.convertTo0to1(value);
    if (normalised != param.getValue())
        param.setValueNotifyingHost(normalised);
}

void RdPiano_juceAudioProcessorEditor::sliderDragStarted(juce::Slider *slider)
{
    if (slider != &alphaDial)
        return;

    dialDragging = true;

    const RdParamId id = panel::modeSpecs[mode].param;
    if (id < kNumRdParams)
    {
        dialGesture = id;
        audioProcessor.param(id).beginChangeGesture();
    }
}

void RdPiano_juceAudioProcessorEditor::sliderDragEnded(juce::Slider *slider)
{
    if (slider != &alphaDial)
        return;

    dialDragging = false;

    if (dialGesture)
    {
        audioProcessor.param(*dialGesture).endChangeGesture();
        dialGesture.reset();
    }

    if (dialPreview)
    {
        if (mode == panel::kModePatch)
            audioProcessor.setCurrentProgram(*dialPreview);
        else if (mode == panel::kModeTune)
            audioProcessor.setMasterTune((int16_t)*dialPreview);
    }

    dialPreview.reset();
    updateValues();
}

void RdPiano_juceAudioProcessorEditor::visibilityChanged() { updateValues(); }

void RdPiano_juceAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster *) { updateValues(); }

void RdPiano_juceAudioProcessorEditor::updateValues()
{
    // Con el dial de parches en la mano manda su posición: el parche todavía no
    // ha cambiado, pero el panel ya enseña cuál se va a poner al soltar.
    const int patch = dialValueOr(panel::kModePatch, audioProcessor.getCurrentProgram());
    const bool alternativeMode = panel::modeSpecs[mode].countsAsAlternative;

    for (int i = 0; i < kNumButtons; i++)
    {
        const ButtonSpec &spec = buttonSpecs[i];
        bool lit = false;

        switch (spec.action)
        {
        case ButtonSpec::kSelectBank:
            lit = !alternativeMode && (spec.value == 0 ? patch < kPatchesPerBank : patch >= kPatchesPerBank);
            break;

        case ButtonSpec::kSelectPatch:
            lit = !alternativeMode && patch % kPatchesPerBank == spec.value;
            break;

        case ButtonSpec::kToggleParam:
            lit = audioProcessor.paramValue(spec.param) >= 0.5f;
            break;

        case ButtonSpec::kCycleModes:
            lit = mode == spec.first || mode == spec.second;
            break;
        }

        buttons[i].setLit(lit);
    }

    const int tune = dialValueOr(panel::kModeTune, (int)audioProcessor.masterTune());
    const int step = mode == panel::kModePatch || mode == panel::kModeTune ? 0 : paramStep(mode);
    const panel::DisplayFrame frame = panel::displayFrame(mode, patch, tune, step);

    lcd.setText(frame.chars);

    // Mover el dial que se está arrastrando lo pelearía con el ratón.
    if (!dialDragging)
        alphaDial.setValue(frame.dial, juce::dontSendNotification);
}
