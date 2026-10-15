#ifndef RDPIANO_ALLOC_COUNTER_H
#define RDPIANO_ALLOC_COUNTER_H

/**
 * @file alloc_counter.h
 * @brief Cuenta las llamadas a `operator new` de todo rdpiano_tests mientras está activo.
 *
 * Es lo que demuestra que render() no reserva. libresample usa malloc y no pasa por aquí: ese lado lo vigila
 * `RdEngineStats::resamplerOpens`.
 */

/** @brief Pone la cuenta a cero y empieza a contar. */
void alloc_count_start();

/**
 * @brief Deja de contar.
 * @return Las reservas hechas desde alloc_count_start(), de cualquier forma de `operator new`.
 */
long alloc_count_stop();

#endif
