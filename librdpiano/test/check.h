#ifndef RDPIANO_CHECK_H
#define RDPIANO_CHECK_H

// Mini-marco de comprobaciones compartido por rdpiano_e2e y rdpiano_tests.
//
// librdpiano no tiene dependencias y no debe ganarlas por las pruebas
// (REFACTORIZACION §17.2): esto es todo el andamiaje que hay.
//
// Uso: cada bloque de comprobaciones recibe (o declara) un `CheckRun checks`,
// registra en él, y al final se imprimen los fallos y se suma `failed()`.
// Las macros CHECK_* escriben sobre un `checks` que debe estar en el ámbito.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include <string>
#include <vector>

// FNV-1a de 64 bits, byte a byte. Es el mismo hash que usa el harness e2e
// para el stream de audio; aquí lo comparten las pruebas que congelan una
// respuesta (efectos, resampler, motor).
struct Fnv1a
{
  unsigned long long h = 0xcbf29ce484222325ull;

  void byte(unsigned char b)
  {
    h ^= b;
    h *= 0x100000001b3ull;
  }

  void bytes(const void *data, size_t n)
  {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++)
      byte(p[i]);
  }

  void i32(int v) { bytes(&v, 4); }

  // Los float se hashean por su patrón de bits: dos streams iguales dan el
  // mismo hash y cualquier bit distinto lo mueve.
  void f32(float v) { bytes(&v, 4); }
};

struct Check
{
  std::string name;
  bool ok;
  std::string detail;
};

// printf a std::string, para el detalle de un fallo.
inline std::string check_fmt(const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return std::string(buf);
}

struct CheckRun
{
  int total = 0;
  std::vector<Check> failures;

  void add(const std::string &name, bool ok, const std::string &detail = "")
  {
    total++;
    if (!ok)
      failures.push_back({name, ok, detail});
  }

  int failed() const { return (int)failures.size(); }

  void print_failures(const char *indent = "    ") const
  {
    for (const Check &c : failures)
      printf("%sFAIL %-24s %s\n", indent, c.name.c_str(), c.detail.c_str());
  }
};

#define CHECK(cond) checks.add(#cond, (cond))
#define CHECK_MSG(cond, ...) checks.add(#cond, (cond), check_fmt(__VA_ARGS__))

#define CHECK_EQ(got, want)                                              \
  checks.add(#got " == " #want, (long long)(got) == (long long)(want),   \
             check_fmt("obtenido %lld, esperado %lld", (long long)(got), \
                       (long long)(want)))

#define CHECK_NEAR(got, want, tol)                                       \
  checks.add(#got " ~= " #want,                                          \
             fabs((double)(got) - (double)(want)) <= (double)(tol),      \
             check_fmt("obtenido %g, esperado %g +/- %g", (double)(got), \
                       (double)(want), (double)(tol)))

#define CHECK_HASH(name, got, want)                                         \
  checks.add(name, (unsigned long long)(got) == (unsigned long long)(want), \
             check_fmt("hash %016llx, esperado %016llx",                    \
                       (unsigned long long)(got), (unsigned long long)(want)))

#endif
