#include "../include/rd_trace.h"

#ifdef RDPIANO_TRACE
#include <stdarg.h>
#include <stdio.h>
#endif

/**
 * @file rd_trace.cpp
 * @brief El destino de la traza: instalable, y stderr por defecto.
 */

namespace
{
#ifdef RDPIANO_TRACE
    /** @brief El destino mientras nadie instale otro. */
    void stderr_sink(const char *message) { fputs(message, stderr); }

    RdTraceSink g_sink = stderr_sink;
#else
    RdTraceSink g_sink = nullptr;
#endif
} // namespace

void rdpiano_set_trace_sink(RdTraceSink sink) { g_sink = sink; }

#ifdef RDPIANO_TRACE

void rdpiano_trace(const char *fmt, ...)
{
    if (g_sink == nullptr)
        return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    g_sink(buf);
}

#endif
