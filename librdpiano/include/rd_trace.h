#ifndef RD_TRACE_H
#define RD_TRACE_H

// Punto único de salida de traza del núcleo (REFACTORIZACION §15).
//
// `librdpiano` se define como "sin dependencias", pero conocía `stdio`: había
// cuatro `printf` activos en rutas que corren **desde el hilo de audio**, y un
// printf toma el lock de stdio y puede bloquear (AUDITORIA §7). El problema de
// fondo no era el coste, era que el núcleo decidía por su cuenta a dónde iba
// la traza.
//
// Ahora no decide: en release RD_TRACE no compila a nada —los argumentos ni
// siquiera se evalúan— y en un build con -DRDPIANO_TRACE la salida va a donde
// diga el que integra la librería.

// Compilando sin RDPIANO_TRACE, `sink` no se usa; la función existe igual para
// que el llamante no tenga que compilarse condicionalmente.
typedef void (*RdTraceSink)(const char *message);

// Instala el destino de la traza. `nullptr` descarta. Sin instalar nada, un
// build con RDPIANO_TRACE escribe en stderr.
void rdpiano_set_trace_sink(RdTraceSink sink);

#ifdef RDPIANO_TRACE

void rdpiano_trace(const char *fmt, ...);
#define RD_TRACE(...) rdpiano_trace(__VA_ARGS__)

#else

// Sin traza: los argumentos no se evalúan, así que no hay coste ni efectos.
#define RD_TRACE(...) ((void)0)

#endif

#endif
