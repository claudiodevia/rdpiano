/*
  ==============================================================================

    Construcción del layout de parámetros desde la tabla de PluginParams.h.

  ==============================================================================
*/

#include "PluginParams.h"

juce::AudioProcessorValueTreeState::ParameterLayout createRdParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumRdParams; i++)
    {
        const RdParamSpec &spec = rdParamSpecs[i];

        // El `versionHint` 1 es el que llevaban los `juce::ParameterID` escritos a
        // mano; cambiarlo movería los ids que VST3 deriva de ellos.
        const juce::ParameterID id{spec.id, 1};

        switch (spec.kind)
        {
        case RdParamSpec::Bool:
            layout.add(std::make_unique<juce::AudioParameterBool>(id, spec.name, spec.defaultValue >= 0.5f));
            break;

        case RdParamSpec::Int:
            layout.add(std::make_unique<juce::AudioParameterInt>(id, spec.name, (int)spec.minValue, (int)spec.maxValue,
                                                                 (int)spec.defaultValue));
            break;

        case RdParamSpec::Float:
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, spec.name, spec.minValue, spec.maxValue,
                                                                   spec.defaultValue));
            break;
        }
    }

    return layout;
}
