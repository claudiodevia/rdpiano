#include "change_scheduler.h"

#include "engine_util.h"
#include "patches.h"

/**
 * @file change_scheduler.cpp
 * @brief La ventana de asentamiento y la máquina del declick.
 */

ChangeScheduler::Due ChangeScheduler::takePending()
{
    Due due;

    int patch = patchRequest.exchange(-1, std::memory_order_acquire);
    if (patch < 0 || patch >= NUM_PATCHES)
        patch = declickPatch;
    if (patch < 0 || patch >= NUM_PATCHES)
        patch = settlePatch;
    if (patch >= 0 && patch < NUM_PATCHES)
        due.patch = patch;

    due.tune = tuneRequest.exchange(kNoTune, std::memory_order_acquire);
    if (due.tune == kNoTune)
        due.tune = declickTune;

    return due;
}

void ChangeScheduler::prepare(double newHostRate)
{
    hostRate = newHostRate;

    declickDownStep = ramp_step(kFadeOutMs, hostRate);
    declickUpStep = ramp_step(kFadeInMs, hostRate);
    declickGain = 1.0f;
    declickPatch = -1;
    declickTune = kNoTune;

    patchSettleFrames = ms_to_frames(kPatchSettleMs, (float)hostRate);
    settlePatch = -1;
    settleFrames = 0;
}

void ChangeScheduler::cancelPatch()
{
    patchRequest.store(-1, std::memory_order_relaxed);
    declickPatch = -1;
    settlePatch = -1;
    settleFrames = 0;
}

void ChangeScheduler::cancelTune()
{
    tuneRequest.store(kNoTune, std::memory_order_relaxed);
    declickTune = kNoTune;
}

bool ChangeScheduler::service(int numFrames, int currentPatch, int16_t currentTune, Due &due)
{
    if (numFrames > 0)
    {
        settleFrames -= numFrames;
        if (settleFrames < 0)
            settleFrames = 0;
    }

    const int requested = patchRequest.exchange(-1, std::memory_order_acquire);
    if (requested >= 0 && requested < NUM_PATCHES)
        settlePatch = requested;

    // La salida sigue sonando mientras la ventana esté abierta: el declick
    // empieza al promover el cambio, no al pedirlo.
    if (settlePatch >= 0 && settleFrames == 0)
    {
        declickPatch = settlePatch == currentPatch ? -1 : settlePatch;
        settlePatch = -1;
        settleFrames = patchSettleFrames;
    }

    const int tune = tuneRequest.exchange(kNoTune, std::memory_order_acquire);
    if (tune != kNoTune)
        declickTune = (int16_t)tune != currentTune ? tune : kNoTune;

    if (declickPatch < 0 && declickTune == kNoTune)
        return false;
    if (declickGain > 0.0f)
        return false;

    due.patch = declickPatch;
    due.tune = declickTune;
    declickPatch = -1;
    declickTune = kNoTune;
    return true;
}

void ChangeScheduler::finish(bool retriggered)
{
    declickUpStep = ramp_step(retriggered ? kFadeInHeldMs : kFadeInMs, hostRate);
}
