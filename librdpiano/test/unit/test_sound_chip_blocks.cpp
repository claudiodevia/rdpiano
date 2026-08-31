// Los tres bloques de SoundChip::update() (REFACTORIZACION §5, §17.5).
//
// El hash del golden dice que *algo* cambió; estos vectores dicen **cuál** de
// los tres sumadores. Se capturaron del código anterior a la extracción, con
// un escenario musical en cuatro parches más un barrido de bordes escritos
// directamente en los registros del chip: envolvente que termina y dispara
// IRQ, el silenciado marcado `investigate`, el wrap de fase, y las dos
// banderas de la voz en sus cuatro combinaciones.
//
// No hacen falta ROMs: los cuatro valores de la wave ROM que consume IC8
// vienen en el propio vector. Solo hace falta la LUT compartida, que es
// función pura del índice.

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "sa_blocks.h"
#include "sa_tables.h"
#include "unit_test.h"

namespace
{

  struct Vector
  {
    int line;

    // entrada
    SA_Part part;
    bool flags_0;
    bool flags_1;

    // salida esperada de IC19
    u32 volume;
    bool irq;
    u32 env_value_out;

    // salida esperada de IC9
    u32 sub_phase_out;
    u32 waverom_addr;
    bool sel_sample_type;
    bool phase_hi;

    // entrada de IC8 (del juego de ROMs con el que se capturó)
    u16 waverom_exp;
    bool exp_sign;
    u16 waverom_delta;
    bool delta_sign;

    // salida esperada de IC8: lo que se sumó, y lo que devolvió el bloque
    s32 sample_added;
    s32 sample_raw;
  };

  std::vector<Vector> load_vectors(std::string *error)
  {
    std::vector<Vector> out;
    std::string path = g_vectors_dir + "/ic_blocks.txt";

    FILE *f = fopen(path.c_str(), "r");
    if (!f)
    {
      *error = "no se encuentra " + path;
      return out;
    }

    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof line, f))
    {
      lineNo++;
      if (line[0] == '#' || line[0] == '\n')
        continue;

      Vector v;
      v.line = lineNo;
      unsigned sub, env, pitch, loop, high, dest, speed, off;
      int f0, f1, irq, sel, hi, expSign, deltaSign;
      unsigned volume, envOut, subOut, addr, wexp, wdelta;
      int added, raw;

      int got = sscanf(line,
                       "%x %x %x %x %x %x %x %x %d %d "
                       "%x %d %x "
                       "%x %x %d %d "
                       "%x %d %x %d %d %d",
                       &sub, &env, &pitch, &loop, &high, &dest, &speed, &off, &f0,
                       &f1, &volume, &irq, &envOut, &subOut, &addr, &sel, &hi,
                       &wexp, &expSign, &wdelta, &deltaSign, &added, &raw);
      if (got != 23)
      {
        *error = "línea " + std::to_string(lineNo) + " mal formada";
        fclose(f);
        return out;
      }

      v.part.sub_phase = sub;
      v.part.env_value = env;
      v.part.pitch_lut_i = (u16)pitch;
      v.part.wave_addr_loop = (u8)loop;
      v.part.wave_addr_high = (u8)high;
      v.part.env_dest = (u8)dest;
      v.part.env_speed = (u8)speed;
      v.part.env_offset = (u8)off;
      v.part.flags_0 = false;
      v.part.flags_1 = false;
      v.flags_0 = f0 != 0;
      v.flags_1 = f1 != 0;

      v.volume = volume;
      v.irq = irq != 0;
      v.env_value_out = envOut;
      v.sub_phase_out = subOut;
      v.waverom_addr = addr;
      v.sel_sample_type = sel != 0;
      v.phase_hi = hi != 0;
      v.waverom_exp = (u16)wexp;
      v.exp_sign = expSign != 0;
      v.waverom_delta = (u16)wdelta;
      v.delta_sign = deltaSign != 0;
      v.sample_added = added;
      v.sample_raw = raw;

      out.push_back(v);
    }

    fclose(f);
    return out;
  }

  // Los flags son comunes a la voz: el bucle los pasa en la parte 0.
  SA_Part flags_part(const Vector &v)
  {
    SA_Part flags;
    flags.flags_0 = v.flags_0;
    flags.flags_1 = v.flags_1;
    return flags;
  }

  // Un fallo por bloque, no uno por vector: 2.200 líneas rojas no dicen nada.
  struct FirstFailure
  {
    int line = -1;
    std::string detail;

    void note(int lineNo, const std::string &d)
    {
      if (line < 0)
      {
        line = lineNo;
        detail = d;
      }
    }
  };

  void report(CheckRun &checks, const char *name, size_t bad, size_t total,
              const FirstFailure &first)
  {
    checks.add(name, bad == 0,
               check_fmt("%zu de %zu vector(es); primero en la línea %d: %s", bad,
                         total, first.line, first.detail.c_str()));
  }

} // namespace

TEST_SUITE(sound_chip_blocks_vectors)
{
  std::string error;
  std::vector<Vector> vectors = load_vectors(&error);

  checks.add("vectores cargados", !vectors.empty(),
             error.empty() ? "fichero vacío" : error);
  if (vectors.empty())
    return;

  // Un fichero que se queda corto en silencio no protege nada.
  CHECK(vectors.size() > 2000);

  const SaTables &tables = sa_tables();

  size_t badIc19 = 0, badIc9 = 0, badIc8 = 0, badSilencing = 0;
  FirstFailure firstIc19, firstIc9, firstIc8, firstSilencing;

  for (const Vector &v : vectors)
  {
    const SA_Part flags = flags_part(v);
    SA_Part part = v.part;

    // --- IC19: volumen, IRQ y avance de la envolvente.
    Ic19Out ic19 = tick_ic19(part, flags);
    if (ic19.volume != v.volume || ic19.irq != v.irq ||
        part.env_value != v.env_value_out)
    {
      badIc19++;
      firstIc19.note(v.line,
                     check_fmt("volume %08x/%08x irq %d/%d env %08x/%08x",
                               ic19.volume, v.volume, ic19.irq ? 1 : 0,
                               v.irq ? 1 : 0, part.env_value, v.env_value_out));
    }

    // --- IC9: fase, dirección de wave ROM y los dos selectores.
    Ic9Out ic9 = tick_ic9(part, flags, tables);
    if (part.sub_phase != v.sub_phase_out ||
        ic9.waverom_addr != v.waverom_addr ||
        ic9.sel_sample_type != v.sel_sample_type || ic9.phase_hi != v.phase_hi)
    {
      badIc9++;
      firstIc9.note(v.line, check_fmt("phase %08x/%08x addr %08x/%08x sel %d/%d "
                                      "hi %d/%d",
                                      part.sub_phase, v.sub_phase_out,
                                      ic9.waverom_addr, v.waverom_addr,
                                      ic9.sel_sample_type ? 1 : 0,
                                      v.sel_sample_type ? 1 : 0,
                                      ic9.phase_hi ? 1 : 0, v.phase_hi ? 1 : 0));
    }

    // --- IC8: la suma logarítmica. Se compara con la muestra *cruda*: si la
    // voz suena o no lo decide el bucle, no el bloque.
    s32 sample = tick_ic8(part, ic19, ic9, v.waverom_exp, v.exp_sign,
                          v.waverom_delta, v.delta_sign, tables);
    if (sample != v.sample_raw)
    {
      badIc8++;
      firstIc8.note(v.line, check_fmt("%d, esperado %d", sample, v.sample_raw));
    }

    // --- Y el hack `investigate`: con env_value a cero, la muestra se calcula
    // y se tira. Está aquí escrito para que se vea, no para que se arregle sin
    // querer: arreglarlo es un cambio de audio.
    s32 added = part.env_value != 0 ? sample : 0;
    if (added != v.sample_added)
    {
      badSilencing++;
      firstSilencing.note(v.line,
                          check_fmt("%d, esperado %d", added, v.sample_added));
    }
  }

  report(checks, "IC19 (envolvente)", badIc19, vectors.size(), firstIc19);
  report(checks, "IC9 (fase)", badIc9, vectors.size(), firstIc9);
  report(checks, "IC8 (suma log)", badIc8, vectors.size(), firstIc8);
  report(checks, "silenciado investigate", badSilencing, vectors.size(),
         firstSilencing);
}

// Los bordes que los vectores tienen que estar cubriendo. Si un recorte futuro
// del fichero se los lleva por delante, que se entere alguien.
TEST_SUITE(sound_chip_blocks_coverage)
{
  std::string error;
  std::vector<Vector> vectors = load_vectors(&error);
  if (vectors.empty())
    return;

  size_t irq = 0, sel = 0, hi = 0, noFlags0 = 0, noFlags1 = 0, silenced = 0,
         speedInv = 0, phaseWrap = 0;

  for (const Vector &v : vectors)
  {
    if (v.irq)
      irq++;
    if (v.sel_sample_type)
      sel++;
    if (v.phase_hi)
      hi++;
    if (!v.flags_0)
      noFlags0++;
    if (!v.flags_1)
      noFlags1++;
    if (v.sample_added == 0 && v.sample_raw != 0)
      silenced++;
    if (v.part.env_speed & 0x80)
      speedInv++;
    if ((v.sub_phase_out >> 16) != (v.part.sub_phase >> 16))
      phaseWrap++;
  }

  CHECK(irq > 100);      // la envolvente termina un segmento
  CHECK(sel > 100);      // selector de tipo de muestra
  CHECK(hi > 100);       // fase alta: fuerza el volumen al máximo
  CHECK(noFlags0 > 50);  // flags_0 = 0: la envolvente se congela
  CHECK(noFlags1 > 50);  // flags_1 = 0: la fase no avanza
  CHECK(speedInv > 50);  // env_speed con el bit de "inverso"
  CHECK(phaseWrap > 50); // el acumulador de fase envuelve
  CHECK(silenced > 0);   // el hack `investigate`
}
