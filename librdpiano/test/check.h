#ifndef RDPIANO_CHECK_H
#define RDPIANO_CHECK_H

/**
 * @file check.h
 * @brief Mini-marco de comprobaciones compartido por rdpiano_e2e y rdpiano_tests.
 *
 * El núcleo no tiene dependencias y tampoco las gana por las pruebas: esto es
 * todo el andamiaje que hay. Cada bloque recibe (o declara) un CheckRun y
 * registra en él; al final se imprimen los fallos y se suma failed().
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include <string>
#include <vector>

/**
 * @brief FNV-1a de 64 bits: el hash con el que el harness congela un stream de audio.
 */
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

    /// Los float se hashean por su patrón de bits: dos streams iguales dan el
    /// mismo hash y cualquier bit distinto lo mueve.
    void f32(float v) { bytes(&v, 4); }
};

/** @brief Una comprobación ya ejecutada. */
struct Check
{
    std::string name;   ///< La expresión comprobada, tal cual se escribió.
    bool ok;            ///< Si pasó.
    std::string detail; ///< Qué se obtuvo y qué se esperaba, si falló.
};

/**
 * @brief printf a std::string, para el detalle de un fallo.
 * @param fmt Formato estilo printf.
 * @return El texto formateado.
 */
inline std::string check_fmt(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return std::string(buf);
}

/** @brief Las comprobaciones de un bloque: cuántas van y cuáles han fallado. */
struct CheckRun
{
    int total = 0;               ///< Comprobaciones registradas.
    std::vector<Check> failures; ///< Sólo las que han fallado.

    /**
     * @brief Registra una comprobación.
     * @param name La expresión comprobada.
     * @param ok Si pasó.
     * @param detail Qué se obtuvo y qué se esperaba; sólo se guarda si falló.
     */
    void add(const std::string &name, bool ok, const std::string &detail = "")
    {
        total++;
        if (!ok)
            failures.push_back({name, ok, detail});
    }

    /** @brief Cuántas han fallado. */
    int failed() const { return (int)failures.size(); }

    /**
     * @brief Imprime los fallos, uno por línea.
     * @param indent Sangrado de cada línea.
     */
    void print_failures(const char *indent = "    ") const
    {
        for (const Check &c : failures)
            printf("%sFAIL %-24s %s\n", indent, c.name.c_str(), c.detail.c_str());
    }
};

#define CHECK(cond) checks.add(#cond, (cond))
#define CHECK_MSG(cond, ...) checks.add(#cond, (cond), check_fmt(__VA_ARGS__))

#define CHECK_EQ(got, want)                                              \
    checks.add(#got " == " #want, (long long)(got) == (long long)(want), \
               check_fmt("obtenido %lld, esperado %lld", (long long)(got), (long long)(want)))

#define CHECK_NEAR(got, want, tol)                                                       \
    checks.add(#got " ~= " #want, fabs((double)(got) - (double)(want)) <= (double)(tol), \
               check_fmt("obtenido %g, esperado %g +/- %g", (double)(got), (double)(want), (double)(tol)))

#define CHECK_HASH(name, got, want)                                           \
    checks.add(name, (unsigned long long)(got) == (unsigned long long)(want), \
               check_fmt("hash %016llx, esperado %016llx", (unsigned long long)(got), (unsigned long long)(want)))

#endif
