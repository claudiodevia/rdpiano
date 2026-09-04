#ifndef SA_TABLES_H
#define SA_TABLES_H

#include <stddef.h>

#include "mame_utils.h"

/**
 * @file sa_tables.h
 * @brief Las dos LUT de IC10/IC11, compartidas por todo el proceso.
 */

/**
 * @brief Las dos LUT: función pura del índice, no dependen de las ROM ni del parche.
 *
 * Generarlas cuesta ~16 ms y 320 KB, así que se hace una vez para todo el
 * proceso. El contenido está fijado por test_sa_tables.cpp.
 */
struct SaTables
{
    uint32_t phase_exp[0x10000];  ///< Exponente para la subfase (ROM IC11).
    uint16_t samples_exp[0x8000]; ///< Exponente para decodificar muestras (ROM IC10).
};

/**
 * @brief Genera las tablas (transcripción a nivel de puertas).
 * @param out Destino; se sobrescribe entero.
 */
void sa_tables_generate(SaTables &out);

/**
 * @brief La instancia compartida, construida la primera vez que se pide.
 * @return Las tablas; magic static, la inicialización es segura entre hilos.
 */
const SaTables &sa_tables();

#endif
