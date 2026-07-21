/*
 * Rainbow Crackalack: cpu_tests_common.c
 *
 * Implements run_cpu_only_tests(), which runs the CPU-only unit tests
 * shared between crackalack_unit_tests and crackalack_cpu_tests.
 *
 * No GPU initialization, context, or kernel is needed for these tests.
 */

#include <stdio.h>

#include "cpu_tests_common.h"
#include "test_golden.h"

/* terminal_color.h defines these as globals; declare them extern here to
 * avoid duplicate-symbol errors at link time when multiple TUs include it. */
extern char *GREEN;
extern char *RED;
extern char *CLR;

#define PRINT_PASSED() printf("%spassed.%s\n", GREEN, CLR)
#define PRINT_FAILED() printf("%sFAILED!%s\n", RED, CLR)

int run_cpu_only_tests(void) {
  int all_passed = 1;

  /* Golden-vector tests (CPU-only, no kernel needed).
   * Pins the canonical math for ntlm_hash, hash_to_index, and
   * index_to_plaintext so backend drift is caught. */
  printf("Running golden vector tests... "); fflush(stdout);
  if (!test_golden()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  return all_passed;
}
