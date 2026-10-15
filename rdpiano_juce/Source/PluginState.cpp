/**
 * @file PluginState.cpp
 * @brief Escritura y lectura saneada de parche y afinación en el árbol del preset.
 */

#include "PluginState.h"
#include "patches.h"

namespace
{
    const juce::Identifier kPatchAttribute{"currentPatch"};
    const juce::Identifier kTuneAttribute{"masterTune"};
} // namespace

juce::ValueTree writeRdState(juce::ValueTree params, const RdPresetExtras &extras)
{
    params.setProperty(kPatchAttribute, extras.patch, nullptr);
    params.setProperty(kTuneAttribute, (int)extras.tune, nullptr);
    return params;
}

RdPresetExtras readRdState(const juce::ValueTree &state)
{
    RdPresetExtras extras;

    const int patch = state.getProperty(kPatchAttribute, 0);
    extras.patch = patch >= 0 && patch < NUM_PATCHES ? patch : 0;
    extras.tune = (int16_t)juce::jlimit(-32768, 32767, (int)state.getProperty(kTuneAttribute, 0));

    return extras;
}
