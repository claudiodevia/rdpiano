/**
 * @file alloc_counter.cpp
 * @brief Sustituye el `operator new` global de rdpiano_tests, en todas sus formas, para contar reservas.
 */

#include "alloc_counter.h"

#include <stddef.h>
#include <stdlib.h>

#include <new>

namespace
{

    bool g_counting = false;
    long g_count = 0;

    /** @brief Reserva contada; nullptr si no hay memoria. */
    void *counted(size_t n, size_t align)
    {
        if (g_counting)
            g_count++;
        if (n == 0)
            n = 1;
        if (align <= alignof(std::max_align_t))
            return malloc(n);
        void *p = nullptr;
        return posix_memalign(&p, align, n) == 0 ? p : nullptr;
    }

    /** @brief Reserva contada que lanza `std::bad_alloc` si no hay memoria, como exige el estándar. */
    void *counted_or_throw(size_t n, size_t align)
    {
        void *p = counted(n, align);
        if (p == nullptr)
            throw std::bad_alloc();
        return p;
    }

} // namespace

void alloc_count_start()
{
    g_count = 0;
    g_counting = true;
}

long alloc_count_stop()
{
    g_counting = false;
    return g_count;
}

void *operator new(size_t n) { return counted_or_throw(n, 0); }
void *operator new[](size_t n) { return counted_or_throw(n, 0); }
void *operator new(size_t n, std::align_val_t a) { return counted_or_throw(n, (size_t)a); }
void *operator new[](size_t n, std::align_val_t a) { return counted_or_throw(n, (size_t)a); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return counted(n, 0); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return counted(n, 0); }
void *operator new(size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { return counted(n, (size_t)a); }
void *operator new[](size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { return counted(n, (size_t)a); }

void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
void operator delete(void *p, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { free(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { free(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { free(p); }
