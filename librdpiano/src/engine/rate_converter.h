#ifndef RD_RATE_CONVERTER_H
#define RD_RATE_CONVERTER_H

/**
 * @file rate_converter.h
 * @brief De la tasa del emulador a la del host: búferes, remuestreadores y corrección de deriva.
 */

struct RdEngineStats;

/**
 * @brief Los búferes del emulador (a la tasa del parche) y su salida remuestreada (a la del host).
 *
 * Todo se reserva en open(), dimensionado para el peor caso de los 16 parches, y nada después: el parche
 * cambia sin volver a abrir.
 */
class RateConverter
{
  public:
    RateConverter() = default;
    ~RateConverter() { close(); }

    RateConverter(const RateConverter &) = delete;
    RateConverter &operator=(const RateConverter &) = delete;

    /**
     * @brief Reserva los búferes y abre los dos remuestreadores para todo el rango de parches a esta tasa.
     *
     * Cada `resample_open()` reserva ~600 KB y calcula un filtro Kaiser: 2,5 ms. Por eso solo aquí.
     *
     * @return Cuántos remuestreadores se abrieron: dos, salvo que falte memoria.
     */
    int open(double hostRate, int maxBlock);

    /** @brief Libera búferes y remuestreadores; open() puede volver a llamarse. */
    void close();

    /**
     * @brief Cuántas muestras del emulador pide un bloque, con la corrección de deriva.
     *
     * Apunta la deriva del bloque, que process() acumula si el bloque se rinde.
     *
     * @param numFrames Muestras del host que pide el anfitrión.
     * @param sourceRate Tasa del emulador.
     * @return Muestras del emulador a generar, o 0 si el bloque no se puede rendir (contado en `stats`).
     */
    int plan(int numFrames, int sourceRate, RdEngineStats &stats);

    /**
     * @brief Remuestrea lo que el emulador dejó en emuLeft()/emuRight() a outLeft()/outRight().
     * @param emuFrames Muestras del emulador, las que dio plan().
     * @param numFrames Muestras del host a producir.
     * @param sourceRate Tasa del emulador.
     * @param stats Donde se cuenta un bloque sin consumir (clic).
     */
    void process(int emuFrames, int numFrames, int sourceRate, RdEngineStats &stats);

    float *emuLeft() { return emuL; }
    float *emuRight() { return emuR; }
    const float *outLeft() const { return outL; }
    const float *outRight() const { return outR; }

    /** @brief El bloque del host más grande que cabe. */
    int outCapacity() const { return outCap; }

    /**
     * @brief Retardo de grupo, en muestras del host.
     *
     * El del peor caso —el parche de 20 kHz—, no el del parche puesto: así el anfitrión no renegocia la
     * latencia en cada cambio de sonido.
     */
    int latency() const { return latencyFrames; }

  private:
    void *resampleL = nullptr;
    void *resampleR = nullptr;

    float *emuL = nullptr;
    float *emuR = nullptr;
    float *outL = nullptr;
    float *outR = nullptr;
    int emuCap = 0;
    int outCap = 0;

    double hostRate = 0;
    int latencyFrames = 0;
    double samplesError = 0; ///< Deriva acumulada entre lo generado y lo que pedía el host.
    double blockError = 0;   ///< La de este bloque, apuntada por plan().
};

#endif
