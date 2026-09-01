// Las dos LUT de IC10/IC11 (REFACTORIZACION §4, §17.5).
//
// Son función pura del índice y se generan una sola vez para todo el proceso.
// Los hashes de abajo se capturaron del código anterior a la fase 1, cuando
// cada SoundChip generaba las tablas en su propio constructor: si cambian, el
// timbre cambia, y no es un test que se regenere para ponerlo verde.

#include "unit_test.h"

#include "sa_tables.h"

static u64 fnv1a(const void *data, size_t n)
{
    const unsigned char *b = (const unsigned char *)data;
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++)
    {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// Capturados con el generador de sound_chip.cpp anterior a la fase 1.
static constexpr u64 PHASE_EXP_HASH = 0x94e2a43f3db60011ull;
static constexpr u64 SAMPLES_EXP_HASH = 0xba26c216dad20d95ull;

TEST_SUITE(sa_tables_hashes)
{
    const SaTables &t = sa_tables();

    CHECK_HASH("phase_exp", fnv1a(t.phase_exp, sizeof t.phase_exp), PHASE_EXP_HASH);
    CHECK_HASH("samples_exp", fnv1a(t.samples_exp, sizeof t.samples_exp), SAMPLES_EXP_HASH);
}

// La instancia compartida tiene que ser la misma para todo el mundo: es lo que
// ahorra los 320 KB y los ~16 ms por SoundChip.
TEST_SUITE(sa_tables_shared) { CHECK(&sa_tables() == &sa_tables()); }

// ...y tiene que coincidir con lo que produce el generador en frío. Esto es lo
// que autorizaría precalcularlas como blob (§4, opción 2): la comparación
// generador-contra-tabla ya está escrita.
TEST_SUITE(sa_tables_generator_matches)
{
    SaTables *fresh = new SaTables();
    sa_tables_generate(*fresh);
    const SaTables &shared = sa_tables();

    size_t phaseDiffs = 0;
    for (size_t i = 0; i < 0x10000; i++)
        if (fresh->phase_exp[i] != shared.phase_exp[i])
            phaseDiffs++;

    size_t sampleDiffs = 0;
    for (size_t i = 0; i < 0x8000; i++)
        if (fresh->samples_exp[i] != shared.samples_exp[i])
            sampleDiffs++;

    CHECK_EQ(phaseDiffs, 0);
    CHECK_EQ(sampleDiffs, 0);

    // Rango: phase_exp usa 19 bits (IC11), samples_exp 15 (IC10).
    u32 phaseMax = 0;
    for (size_t i = 0; i < 0x10000; i++)
        if (fresh->phase_exp[i] > phaseMax)
            phaseMax = fresh->phase_exp[i];
    u32 sampleMax = 0;
    for (size_t i = 0; i < 0x8000; i++)
        if (fresh->samples_exp[i] > sampleMax)
            sampleMax = fresh->samples_exp[i];

    CHECK(phaseMax < (1u << 19));
    CHECK(sampleMax < (1u << 15));

    delete fresh;
}
