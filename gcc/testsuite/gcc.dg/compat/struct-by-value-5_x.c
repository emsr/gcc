#ifdef DBG
#include <stdio.h>
#define DEBUG_FPUTS(x) fputs (x, stdout)
#define DEBUG_DOT putc ('.', stdout)
#define DEBUG_NL putc ('\n', stdout)
#else
#define DEBUG_FPUTS(x)
#define DEBUG_DOT
#define DEBUG_NL
#endif

#include "fp-struct-defs.h"
#include "fp-struct-check.h"
#include "fp-struct-test-by-value-x.h"

extern void abort (void);

DEFS(f, float)
CHECKS(f, float)

TEST(Sf1, float)
TEST(Sf2, float)
TEST(Sf3, float)
TEST(Sf4, float)
TEST(Sf5, float)
TEST(Sf6, float)
TEST(Sf7, float)
TEST(Sf8, float)
TEST(Sf9, float)
TEST(Sf10, float)
TEST(Sf11, float)
TEST(Sf12, float)
TEST(Sf13, float)
TEST(Sf14, float)
TEST(Sf15, float)
TEST(Sf16, float)

#undef T

void
struct_by_value_5_x ()
{
#define T(TYPE, MTYPE) testit##TYPE ();

T(Sf1, float)
T(Sf2, float)
T(Sf3, float)
T(Sf4, float)
T(Sf5, float)
T(Sf6, float)
T(Sf7, float)
T(Sf8, float)
T(Sf9, float)
T(Sf10, float)
T(Sf11, float)
T(Sf12, float)
T(Sf13, float)
T(Sf14, float)
T(Sf15, float)
T(Sf16, float)

#undef T
}
