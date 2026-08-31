#ifndef RDPIANO_UNIT_TEST_H
#define RDPIANO_UNIT_TEST_H

// Registro de suites de rdpiano_tests. Cada test_*.cpp escribe:
//
//   TEST_SUITE(nombre_de_la_suite)
//   {
//     CHECK(algo);
//   }
//
// y la suite se registra sola. main.cpp las recorre; --filter SUBCADENA
// selecciona un subconjunto.
//
// A diferencia del harness e2e, estas pruebas no emulan audio: la suite entera
// tiene que terminar en menos de un segundo (REFACTORIZACION §17.2).

#include <string>
#include <vector>

#include "check.h"

typedef void (*TestFn)(CheckRun &);

struct TestSuite
{
  const char *name;
  TestFn fn;
};

std::vector<TestSuite> &test_registry();

struct TestRegistrar
{
  TestRegistrar(const char *name, TestFn fn)
  {
    test_registry().push_back({name, fn});
  }
};

#define TEST_SUITE(name)                                                      \
  static void name(CheckRun &checks);                                         \
  static TestRegistrar name##_registrar(#name, name);                         \
  static void name(CheckRun &checks)

// Directorio pasado con --roms; las suites que no leen ROMs lo ignoran.
extern std::string g_roms_dir;

#endif
