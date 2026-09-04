#ifndef RD_TRACE_H
#define RD_TRACE_H

/**
 * @file rd_trace.h
 * @brief Punto único de salida de traza del núcleo: el núcleo no decide a dónde va.
 *
 * En release RD_TRACE no compila a nada —ni se evalúan los argumentos—; con
 * -DRDPIANO_TRACE la salida va donde diga quien integra la librería.
 */

/** @brief Destino de una línea de traza ya formateada. */
typedef void (*RdTraceSink)(const char *message);

/**
 * @brief Instala el destino de la traza.
 *
 * La función existe compile o no con RDPIANO_TRACE, para que el llamante no
 * tenga que compilarse condicionalmente.
 *
 * @param sink Destino; `nullptr` descarta. Sin instalar nada, un build con
 *             RDPIANO_TRACE escribe en stderr.
 */
void rdpiano_set_trace_sink(RdTraceSink sink);

#ifdef RDPIANO_TRACE

/**
 * @brief Formatea y emite una línea de traza.
 * @param fmt Formato estilo printf.
 */
void rdpiano_trace(const char *fmt, ...);
#define RD_TRACE(...) rdpiano_trace(__VA_ARGS__)

#else

/// Sin traza: los argumentos no se evalúan, así que no hay coste ni efectos.
#define RD_TRACE(...) ((void)0)

#endif

#endif
