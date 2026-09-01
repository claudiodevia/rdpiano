#ifndef SA_TABLES_H
#define SA_TABLES_H

#include <stddef.h>

#include "mame_utils.h"

// Las dos LUT de IC10/IC11: función pura del índice, no dependen de las ROM ni
// del parche. Generarlas cuesta ~16 ms y 320 KB, así que se hace una vez para
// todo el proceso. El contenido está fijado por test_sa_tables.cpp.
struct SaTables
{
    // exp para la subfase (ROM IC11)
    uint32_t phase_exp[0x10000];
    // exp para decodificar muestras (ROM IC10)
    uint16_t samples_exp[0x8000];
};

// Genera las tablas en `out` (transcripción a nivel de puertas).
void sa_tables_generate(SaTables &out);

// La instancia compartida, construida la primera vez que se pide (magic
// static: la inicialización es segura entre hilos).
const SaTables &sa_tables();

#endif
