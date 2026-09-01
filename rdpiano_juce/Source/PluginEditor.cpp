#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int bgWidth = 6140;
static int bgHeight = 1503;
static float scaleFactor = 5;
static int uiWidth = bgWidth / scaleFactor;
static int uiHeight = bgHeight / scaleFactor;

//==============================================================================
// El panel entero, en coordenadas del fondo. Es la única copia: el constructor,
// `resized()`, `buttonClicked()` y `updateValues()` recorren esta tabla.
typedef RdPiano_juceAudioProcessorEditor Editor;

const Editor::ButtonSpec Editor::buttonSpecs[Editor::kNumButtons] = {
    // MKS-20 / MK-80: los dos bancos de ocho parches
    {{2602, 806, 248, 248}, {2598, 800, 257, 258}, ButtonSpec::kSelectBank, 0, kVolume, kModePatch, kModePatch},
    {{2602, 1068, 248, 248}, {2598, 1058, 257, 260}, ButtonSpec::kSelectBank, 8, kVolume, kModePatch, kModePatch},

    // Los ocho botones de parche, dentro del banco que esté activo
    {{1504, 806, 250, 248}, {1478, 800, 290, 260}, ButtonSpec::kSelectPatch, 0, kVolume, kModePatch, kModePatch},
    {{1764, 806, 248, 248}, {1759, 800, 262, 260}, ButtonSpec::kSelectPatch, 1, kVolume, kModePatch, kModePatch},
    {{2024, 806, 248, 248}, {2018, 800, 262, 260}, ButtonSpec::kSelectPatch, 2, kVolume, kModePatch, kModePatch},
    {{2284, 806, 248, 248}, {2279, 800, 262, 260}, ButtonSpec::kSelectPatch, 3, kVolume, kModePatch, kModePatch},
    {{1504, 1068, 250, 248}, {1478, 1058, 290, 260}, ButtonSpec::kSelectPatch, 4, kVolume, kModePatch, kModePatch},
    {{1764, 1068, 248, 248}, {1759, 1058, 262, 260}, ButtonSpec::kSelectPatch, 5, kVolume, kModePatch, kModePatch},
    {{2024, 1068, 248, 248}, {2018, 1058, 262, 260}, ButtonSpec::kSelectPatch, 6, kVolume, kModePatch, kModePatch},
    {{2284, 1068, 248, 248}, {2279, 1058, 262, 260}, ButtonSpec::kSelectPatch, 7, kVolume, kModePatch, kModePatch},

    // TUNE: un único modo, así que entra y sale del mismo (first == second)
    {{669, 295, 273, 273}, {669, 295, 273, 273}, ButtonSpec::kCycleModes, 0, kVolume, kModeTune, kModeTune},

    // Chorus, trémolo y EFX: on/off arriba, rate/depth abajo
    {{2917, 806, 248, 248}, {2917, 800, 262, 262}, ButtonSpec::kToggleParam, 0, kChorusEnabled, kModePatch, kModePatch},
    {{2917, 1068, 248, 248},
     {2917, 1058, 262, 262},
     ButtonSpec::kCycleModes,
     0,
     kVolume,
     kModeChorusRate,
     kModeChorusDepth},
    {{3229, 806, 248, 248},
     {3229, 800, 262, 262},
     ButtonSpec::kToggleParam,
     0,
     kTremoloEnabled,
     kModePatch,
     kModePatch},
    {{3229, 1068, 248, 248},
     {3229, 1058, 262, 262},
     ButtonSpec::kCycleModes,
     0,
     kVolume,
     kModeTremoloRate,
     kModeTremoloDepth},
    {{3547, 806, 248, 248}, {3547, 800, 262, 262}, ButtonSpec::kToggleParam, 0, kEfxEnabled, kModePatch, kModePatch},
    {{3547, 1068, 248, 248},
     {3547, 1058, 262, 262},
     ButtonSpec::kCycleModes,
     0,
     kVolume,
     kModePhaserRate,
     kModePhaserDepth},
};

// Las etiquetas ocupan 15 columnas exactas: la fila de arriba es etiqueta más
// el número del paso alineado a la derecha en dos columnas.
const Editor::ModeSpec Editor::modeSpecs[Editor::kNumDisplayModes] = {
    {nullptr, kVolume, false}, // kModePatch
    {nullptr, kVolume, true},  // kModeTune
    {"CHORUS RATE    ", kChorusRate, true},
    {"CHORUS DEPTH   ", kChorusDepth, true},
    {"TREMOLO RATE   ", kTremoloRate, true},
    {"TREMOLO DEPTH  ", kTremoloDepth, true},
    {"PHASER RATE    ", kEfxPhaserRate, false},
    {"PHASER DEPTH   ", kEfxPhaserDepth, false},
};

// Los 16 parches como los enseña el display: dos filas de 17. No es
// `patchNames` de patches.h, que usa el formato corto "MKS-20: Piano 1".
static const char *const displayPatchNames[NUM_PATCHES] = {
    "MKS-20           Piano 1          ", "MKS-20           Piano 2          ", "MKS-20           Piano 3          ",
    "MKS-20           Harpsichord      ", "MKS-20           Clavi            ", "MKS-20           Vibraphone       ",
    "MKS-20           E-Piano 1        ", "MKS-20           E-Piano 2        ", "MK-80            Classic          ",
    "MK-80            Special          ", "MK-80            Blend            ", "MK-80            Contemporary     ",
    "MK-80            A. Piano 1       ", "MK-80            A. Piano 2       ", "MK-80            Clavi            ",
    "MK-80            Vibraphone       "};

// Copia `text` en `line` a partir de `at`, sin salirse de las 34 columnas.
static void lcdPut(uint8_t (&line)[Lcd::kChars], int at, const char *text)
{
    for (int i = 0; text[i] != '\0' && at + i < Lcd::kChars; i++)
        line[at + i] = (uint8_t)text[i];
}

//==============================================================================
RdPiano_juceAudioProcessorEditor::RdPiano_juceAudioProcessorEditor(RdPiano_juceAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    addAndMakeVisible(lcd);

    for (int i = 0; i < kNumButtons; i++)
    {
        addAndMakeVisible(buttons[i]);
        buttons[i].addListener(this);
    }

    addAndMakeVisible(alphaDial);
    alphaDial.setLookAndFeel(&knobLF);
    alphaDial.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    alphaDial.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    alphaDial.setRange(-1, 1);
    alphaDial.addListener(this);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setRange(0, 1);
    volumeSlider.addListener(this);
    volumeSlider.setAlpha(0);

    setResizeLimits(uiWidth, uiHeight, uiWidth, uiHeight);
    setSize(uiWidth, uiHeight);

    updateValues();
    p.addChangeListener(this);
}

RdPiano_juceAudioProcessorEditor::~RdPiano_juceAudioProcessorEditor()
{
    alphaDial.setLookAndFeel(nullptr);
    audioProcessor.removeChangeListener(this);
}

void RdPiano_juceAudioProcessorEditor::resized()
{
    float sfC = (float)bgWidth / getBounds().getWidth();

    lcd.setScale(sfC);
    lcd.setBounds((1984 + 60) / sfC, (272 + 50) / sfC, (4 + 1394 - 60 * 2) / sfC, (4 + 309 - 40 * 2) / sfC);

    for (int i = 0; i < kNumButtons; i++)
    {
        const ButtonSpec &spec = buttonSpecs[i];
        buttons[i].setBounds(spec.bounds[0] / sfC, spec.bounds[1] / sfC, spec.bounds[2] / sfC, spec.bounds[3] / sfC);
        buttons[i].position(spec.art[0], spec.art[1], spec.art[2], spec.art[3], sfC);
    }

    alphaDial.setBounds((204.564) * (6140 / 311.92) / sfC, (24.061) * (6140 / 311.92) / sfC,
                        (39.145) * (6140 / 311.92) / sfC, (39.145) * (6140 / 311.92) / sfC);
    volumeSlider.setBounds(1188 / sfC, 660 / sfC, 100 / sfC, 656 / sfC);
}

void RdPiano_juceAudioProcessorEditor::paint(juce::Graphics &g)
{
    float sfC = (float)bgWidth / getBounds().getWidth();

    g.drawImage(juce::ImageCache::getFromMemory(BinaryData::background_png, BinaryData::background_pngSize),
                getLocalBounds().toFloat());

    // Volume
    float volumeY = 660 / sfC + (1 - audioProcessor.paramValue(kVolume)) * (656 - 131) / sfC;
    g.drawImage(juce::ImageCache::getFromMemory(BinaryData::interactable_png, BinaryData::interactable_pngSize),
                1188 / sfC, volumeY, 100 / sfC, 131 / sfC, 1188, 1179, 100, 131);
}

void RdPiano_juceAudioProcessorEditor::buttonClicked(juce::Button *button)
{
    for (int i = 0; i < kNumButtons; i++)
    {
        if (button != &buttons[i])
            continue;

        const ButtonSpec &spec = buttonSpecs[i];
        const DisplayMode previous = mode;

        // Cualquier pulsación saca del modo en el que estuviera el display; sólo
        // kCycleModes vuelve a entrar en uno.
        mode = kModePatch;

        switch (spec.action)
        {
        case ButtonSpec::kSelectBank:
            audioProcessor.setCurrentProgram(spec.value);
            break;

        case ButtonSpec::kSelectPatch:
            audioProcessor.setCurrentProgram(spec.value + (audioProcessor.currentPatch >= 8 ? 8 : 0));
            break;

        case ButtonSpec::kToggleParam:
            audioProcessor.setParamValue(spec.param, audioProcessor.paramValue(spec.param) >= 0.5f ? 0.0f : 1.0f);
            updateValues();
            break;

        case ButtonSpec::kCycleModes:
            // ninguno -> first -> second -> ninguno. Con first == second (TUNE) la
            // segunda posición no existe y el ciclo es de dos.
            if (previous != spec.first && previous != spec.second)
                mode = spec.first;
            else if (previous == spec.first && spec.second != spec.first)
                mode = spec.second;
            updateValues();
            break;
        }

        return;
    }
}

// El paso 0..14 que enseña el display para el parámetro de este modo. Se
// calcula sobre el valor normalizado, así que sirve igual para los rangos
// enteros 0..14 del chorus y del trémolo y para los 0..1 continuos del phaser.
int RdPiano_juceAudioProcessorEditor::paramStep(DisplayMode m) const
{
    const RdParamId id = modeSpecs[m].param;
    const float normalised = audioProcessor.param(id).convertTo0to1(audioProcessor.paramValue(id));
    return juce::jlimit(0, kParamSteps - 1, juce::roundToInt(normalised * (kParamSteps - 1)));
}

// "ETIQUETA        7" / " ______█________ ": la línea de parámetro que antes
// se escribía seis veces, una por modo.
void RdPiano_juceAudioProcessorEditor::renderParamLine(uint8_t (&line)[Lcd::kChars], const ModeSpec &spec,
                                                       int step) const
{
    memset(line, ' ', Lcd::kChars);
    lcdPut(line, 0, spec.label);

    char number[4];
    snprintf(number, sizeof number, "%2d", step + 1);
    lcdPut(line, Lcd::kColumns - 2, number);

    for (int i = 0; i < kParamSteps; i++)
        line[Lcd::kColumns + 1 + i] = '_';

    // El marcador es el carácter 0xff del juego del display: por eso la línea es
    // un búfer de bytes y no una `juce::String`, que lo codificaría en UTF-8.
    line[Lcd::kColumns + 1 + step] = 0xff;
}

void RdPiano_juceAudioProcessorEditor::sliderValueChanged(juce::Slider *slider)
{
    if (slider == &volumeSlider)
    {
        audioProcessor.setParamValue(kVolume, (float)volumeSlider.getValue());
        return;
    }

    if (slider != &alphaDial)
        return;

    const double value = alphaDial.getValue(); // -1..1

    if (mode == kModePatch)
    {
        audioProcessor.setCurrentProgram((int)((value + 1) * 8));
        return;
    }

    if (mode == kModeTune)
    {
        audioProcessor.setMasterTune((int16_t)(value * 32767.0));
        return;
    }

    // Los seis modos de parámetro comparten el mapeo del dial: -1..1 repartido
    // en los 15 pasos que enseña el display.
    const int step = juce::jlimit(0, kParamSteps - 1, (int)std::floor((value / 2.0 + 0.5) * (kParamSteps - 1)));
    const RdParamId id = modeSpecs[mode].param;
    audioProcessor.setParamValue(id, audioProcessor.param(id).convertFrom0to1((float)step / (kParamSteps - 1)));
    updateValues();
}

void RdPiano_juceAudioProcessorEditor::visibilityChanged() { updateValues(); }

void RdPiano_juceAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster *) { updateValues(); }

void RdPiano_juceAudioProcessorEditor::updateValues()
{
    const int patch = audioProcessor.currentPatch;
    const bool alternativeMode = modeSpecs[mode].countsAsAlternative;

    for (int i = 0; i < kNumButtons; i++)
    {
        const ButtonSpec &spec = buttonSpecs[i];
        bool lit = false;

        switch (spec.action)
        {
        case ButtonSpec::kSelectBank:
            lit = !alternativeMode && (spec.value == 0 ? patch < 8 : patch >= 8);
            break;

        case ButtonSpec::kSelectPatch:
            lit = !alternativeMode && patch % 8 == spec.value;
            break;

        case ButtonSpec::kToggleParam:
            lit = audioProcessor.paramValue(spec.param) >= 0.5f;
            break;

        case ButtonSpec::kCycleModes:
            lit = mode == spec.first || mode == spec.second;
            break;
        }

        buttons[i].enabled = lit;
    }

    uint8_t line[Lcd::kChars];
    double dial = 0;

    if (mode == kModePatch)
    {
        memset(line, ' ', Lcd::kChars);
        if (patch >= 0 && patch < NUM_PATCHES)
            lcdPut(line, 0, displayPatchNames[patch]);
        dial = patch / 16.0 * 2.0 - 1.0;
    }
    else if (mode == kModeTune)
    {
        // 442 Hz de referencia y +/-3,85 Hz de recorrido: es lo que abarca el
        // parámetro de afinación del firmware.
        const juce::String hz(442.0 + audioProcessor.masterTune / 32767.0 * 3.85, 1);

        memset(line, ' ', Lcd::kChars);
        lcdPut(line, 0, "TUNING");
        lcdPut(line, Lcd::kColumns, hz.toRawUTF8());
        lcdPut(line, Lcd::kColumns + hz.length(), "Hz");
        dial = audioProcessor.masterTune / 32767.0;
    }
    else
    {
        const int step = paramStep(mode);
        renderParamLine(line, modeSpecs[mode], step);
        dial = step / (double)(kParamSteps - 1) * 2.0 - 1.0;
    }

    lcd.setText(line);
    alphaDial.setValue(dial, juce::dontSendNotification);
    volumeSlider.setValue(audioProcessor.paramValue(kVolume), juce::dontSendNotification);

    this->repaint();
}
