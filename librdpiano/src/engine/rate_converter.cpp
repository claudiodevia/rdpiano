#include "rate_converter.h"

#include <math.h>

#include "engine_util.h"
#include "patches.h"
#include "rd_engine.h"
#include "rd_trace.h"
#include "resample/libresample.h"

/**
 * @file rate_converter.cpp
 * @brief Dimensionado, apertura y uso de los dos remuestreadores.
 */

int RateConverter::open(double newHostRate, int maxBlock)
{
    close();
    hostRate = newHostRate;

    // El búfer intermedio va a la tasa del EMULADOR: el factor es
    // sourceRate/hostRate. Peor caso, el parche más rápido, más el margen de
    // numFrames/4 que puede añadir la corrección de deriva.
    const double worstRatio = (double)MAX_PATCH_RATE / hostRate;
    emuCap = (int)ceil(maxBlock * worstRatio) + maxBlock / 4 + 4;
    if (emuCap < 4)
        emuCap = 4;
    outCap = maxBlock < 1 ? 1 : maxBlock;

    emuL = new float[emuCap]();
    emuR = new float[emuCap]();
    outL = new float[outCap]();
    outR = new float[outCap]();

    samplesError = 0;
    blockError = 0;

    // resample_process() acepta un factor variable dentro del rango; el filtro
    // no depende de él.
    const double minFactor = hostRate / MAX_PATCH_RATE;
    const double maxFactor = hostRate / MIN_PATCH_RATE;
    resampleL = resample_open(1, minFactor, maxFactor);
    resampleR = resample_open(1, minFactor, maxFactor);

    // `Xoff` va en muestras de ENTRADA: se convierte con la tasa del emulador.
    const int filterWidth = resampleL ? resample_get_filter_width(resampleL) : 0;
    latencyFrames = (int)(filterWidth * hostRate / MIN_PATCH_RATE + 0.5);

    return (resampleL ? 1 : 0) + (resampleR ? 1 : 0);
}

void RateConverter::close()
{
    delete[] emuL;
    delete[] emuR;
    delete[] outL;
    delete[] outR;
    emuL = emuR = outL = outR = nullptr;
    emuCap = 0;
    outCap = 0;

    if (resampleL)
        resample_close(resampleL);
    if (resampleR)
        resample_close(resampleR);
    resampleL = resampleR = nullptr;
}

int RateConverter::plan(int numFrames, int sourceRate, RdEngineStats &stats)
{
    const double renderBufferFramesFloat = (double)numFrames / hostRate * sourceRate;
    int renderBufferFrames = (int)ceil(renderBufferFramesFloat);
    double currentError = renderBufferFrames - renderBufferFramesFloat;

    const int limit = numFrames / 4;
    if (samplesError > limit && renderBufferFrames > limit)
    {
        renderBufferFrames -= limit;
        currentError -= limit;
    }
    else if (-samplesError > limit)
    {
        renderBufferFrames += limit;
        currentError += limit;
    }

    if (renderBufferFrames < 2)
    {
        stats.tooFewFrames.fetch_add(1, std::memory_order_relaxed);
        RD_TRACE("engine: no hay muestras que generar (%d)", renderBufferFrames);
        return 0;
    }
    if (renderBufferFrames > emuCap)
    {
        stats.tooManyFrames.fetch_add(1, std::memory_order_relaxed);
        RD_TRACE("engine: demasiadas muestras que generar (%d > %d)", renderBufferFrames, emuCap);
        return 0;
    }

    blockError = currentError;
    return renderBufferFrames;
}

void RateConverter::process(int emuFrames, int numFrames, int sourceRate, RdEngineStats &stats)
{
    // `emuL`/`emuR` no se limpian: el emulador asigna las `emuFrames` primeras
    // posiciones y resample_process() no lee más. La salida sí: el remuestreador
    // puede devolver menos de `numFrames` y la cola se lee igual.
    silence(outL, outR, numFrames);

    const double ratio = hostRate / sourceRate;

    int inUsedL = 0;
    int inUsedR = 0;
    [[maybe_unused]] const int out = resample_process(resampleL, ratio, emuL, emuFrames, 0, &inUsedL, outL, numFrames);
    resample_process(resampleR, ratio, emuR, emuFrames, 0, &inUsedR, outR, numFrames);
    if (inUsedL != inUsedR)
        RD_TRACE("engine: los canales consumieron distinto (%d y %d)", inUsedL, inUsedR);

    samplesError += blockError;
    if (inUsedL == 0 || inUsedR == 0)
    {
        samplesError = 0;
        stats.clicks.fetch_add(1, std::memory_order_relaxed);
        RD_TRACE("engine: click (%d)", out);
    }
}
