/*
  ==============================================================================

    Los diez parámetros del plugin, declarados una sola vez
    (REFACTORIZACION §9).

    Hasta la fase 2 la misma información vivía en tres sitios: once
    `addParameter` en el constructor, un XML escrito atributo a atributo en
    `getStateInformation`, y una lista de validaciones en `setStateInformation`
    con un segundo juego de valores por defecto que **no coincidía** con el del
    constructor (`chorusRate` arrancaba en 5 y se restauraba a 1). Cargar una
    sesión antigua cambiaba el sonido.

    Aquí la declaración es una tabla y el valor por defecto de cada parámetro
    sale de `RdEngineParams`, que es el POD que lee el motor: plugin y núcleo no
    pueden discrepar sobre qué es "de fábrica". El resto —serialización,
    validación de rango— lo hace `juce::AudioProcessorValueTreeState`.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "rd_engine.h"

// Índice de cada parámetro en la tabla. Es también el orden en que el host los
// ve, así que sólo se añade por el final.
enum RdParamId
{
  kVolume = 0,
  kChorusEnabled,
  kChorusRate,
  kChorusDepth,
  kTremoloEnabled,
  kTremoloRate,
  kTremoloDepth,
  kEfxEnabled,
  kEfxPhaserRate,
  kEfxPhaserDepth,

  kNumRdParams
};

struct RdParamSpec
{
  enum Kind
  {
    Float,
    Int,
    Bool
  };

  const char *id;   // el que va en el preset: renombrarlo rompe las sesiones
  const char *name; // el que ve el usuario en el host
  Kind kind;
  float minValue;
  float maxValue;
  float defaultValue;
};

// Los valores de fábrica son los del motor, literalmente.
inline constexpr RdEngineParams kEngineDefaults{};

inline constexpr RdParamSpec rdParamSpecs[kNumRdParams] = {
    {"volume", "Volume", RdParamSpec::Float, 0.0f, 1.0f,
     kEngineDefaults.volume},
    {"chorusEnabled", "Chorus Enabled", RdParamSpec::Bool, 0.0f, 1.0f,
     kEngineDefaults.chorusEnabled ? 1.0f : 0.0f},
    {"chorusRate", "Chorus Rate", RdParamSpec::Int, 0.0f, 14.0f,
     (float)kEngineDefaults.chorusRate},
    {"chorusDepth", "Chorus Depth", RdParamSpec::Int, 0.0f, 14.0f,
     (float)kEngineDefaults.chorusDepth},
    {"tremoloEnabled", "Tremolo Enabled", RdParamSpec::Bool, 0.0f, 1.0f,
     kEngineDefaults.tremoloEnabled ? 1.0f : 0.0f},
    {"tremoloRate", "Tremolo Rate", RdParamSpec::Int, 0.0f, 14.0f,
     (float)kEngineDefaults.tremoloRate},
    {"tremoloDepth", "Tremolo Depth", RdParamSpec::Int, 0.0f, 14.0f,
     (float)kEngineDefaults.tremoloDepth},
    {"efxEnabled", "EFX Enabled", RdParamSpec::Bool, 0.0f, 1.0f,
     kEngineDefaults.efxEnabled ? 1.0f : 0.0f},
    {"efxPhaserRate", "Phaser Rate", RdParamSpec::Float, 0.0f, 1.0f,
     kEngineDefaults.efxPhaserRate},
    {"efxPhaserDepth", "Phaser Depth", RdParamSpec::Float, 0.0f, 1.0f,
     kEngineDefaults.efxPhaserDepth},
};

// Coherencia de la tabla, en compilación. El rango de los tres pares
// rate/depth es 0..14 porque es lo que indexan `chorusRateToMsPeriod` y las
// tablas de `lsp/`; el editor lo muestra como 1..15.
namespace rd_params_detail
{
  constexpr bool defaults_in_range()
  {
    for (int i = 0; i < kNumRdParams; i++)
      if (rdParamSpecs[i].defaultValue < rdParamSpecs[i].minValue ||
          rdParamSpecs[i].defaultValue > rdParamSpecs[i].maxValue)
        return false;
    return true;
  }

  constexpr bool ids_present()
  {
    for (int i = 0; i < kNumRdParams; i++)
      if (rdParamSpecs[i].id == nullptr || rdParamSpecs[i].id[0] == '\0' ||
          rdParamSpecs[i].name == nullptr || rdParamSpecs[i].name[0] == '\0')
        return false;
    return true;
  }
} // namespace rd_params_detail

static_assert(rd_params_detail::defaults_in_range());
static_assert(rd_params_detail::ids_present());

// El layout que recibe el AudioProcessorValueTreeState, construido desde la
// tabla. La serialización y los valores por defecto salen de aquí y de ningún
// otro sitio.
juce::AudioProcessorValueTreeState::ParameterLayout createRdParameterLayout();
