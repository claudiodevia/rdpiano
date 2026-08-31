#include "../include/rd_trace.h"

#ifdef RDPIANO_TRACE
#include <stdarg.h>
#include <stdio.h>
#endif

namespace
{
  RdTraceSink g_sink = nullptr;
  bool g_sink_set = false;
} // namespace

void rdpiano_set_trace_sink(RdTraceSink sink)
{
  g_sink = sink;
  g_sink_set = true;
}

#ifdef RDPIANO_TRACE

void rdpiano_trace(const char *fmt, ...)
{
  if (g_sink_set && g_sink == nullptr)
    return;

  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);

  if (g_sink)
    g_sink(buf);
  else
    fputs(buf, stderr);
}

#endif
