#ifndef SA_TABLES_H
#define SA_TABLES_H

#include <stddef.h>

#include "mame_utils.h"

// Las dos LUT de IC10/IC11 son función pura del índice: no dependen de las ROM
// ni del parche. Generarlas cuesta ~16 ms y ocupa 320 KB, y hasta la fase 1 se
// pagaba una vez por cada SoundChip construido —es decir, por cada instancia
// del plugin que el DAW crea para escanear o al duplicar una pista.
//
// Aquí se generan una sola vez y se comparten por referencia constante.
// REFACTORIZACION §4. El contenido está fijado por test_sa_tables.cpp.
struct SaTables
{
    // exp para la subfase (ROM IC11)
    uint32_t phase_exp[0x10000];
    // exp para decodificar muestras (ROM IC10)
    uint16_t samples_exp[0x8000];
};

// Genera las tablas en `out`. Es la transcripción a nivel de puertas; se
// expone para que una prueba pueda comparar generador contra tabla compartida.
void sa_tables_generate(SaTables &out);

// La instancia compartida. Se construye la primera vez que se pide; los
// magic statics de C++11 hacen la inicialización segura entre hilos.
const SaTables &sa_tables();

#endif
