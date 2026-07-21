/*
 * Rainbow Crackalack: crackalack_cpu_tests.c
 *
 * Standalone CPU-only test binary.  Runs the CPU-only golden-vector tests
 * without any GPU initialization.  Designed for CI environments that have no
 * GPU (only build-essential, libgcrypt, and OpenCL headers are required).
 *
 * For the full GPU+CPU suite see crackalack_unit_tests.
 */

#include <stdio.h>

#include "cpu_tests_common.h"
#include "terminal_color.h"
#include "version.h"


int main(void) {
  int all_passed;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  all_passed = run_cpu_only_tests();

  if (all_passed)
    printf("\n\t%sALL CPU UNIT TESTS PASS!%s\n\n", GREENB, CLR);
  else
    printf("\n\t%sSome CPU unit tests failed!%s  :(\n\n", REDB, CLR);

  return all_passed ? 0 : -1;
}
