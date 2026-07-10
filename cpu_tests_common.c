/*
 * Rainbow Crackalack: cpu_tests_common.c
 *
 * Implements run_cpu_only_tests(), which runs the 7 CPU-only unit tests
 * shared between crackalack_unit_tests and crackalack_cpu_tests.
 *
 * No GPU initialization, context, or kernel is needed for these tests.
 */

#include <stdio.h>

#include "cpu_tests_common.h"
#include "test_challenge_host.h"
#include "test_misc.h"
#include "test_bloom.h"
#include "test_sort.h"
#include "test_decompress.h"
#include "test_precompute_collate.h"
#include "test_markov.h"
#include "test_mask_parse.h"
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

  /* Challenge host tests (CPU-only, no kernel needed). */
  printf("Running challenge host tests... "); fflush(stdout);
  if (!test_challenge_host()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Misc tests (CPU-only, no kernel needed). */
  printf("Running misc tests... "); fflush(stdout);
  if (!test_misc()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Bloom tests (CPU-only, no kernel needed). */
  printf("Running bloom tests... "); fflush(stdout);
  if (!test_bloom()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Sort utility tests (CPU-only, no kernel needed). */
  printf("Running sort utility tests... "); fflush(stdout);
  if (!test_sort()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Decompression tests (CPU-only, no kernel needed). */
  printf("Running decompress tests... "); fflush(stdout);
  if (!test_decompress()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Batched-precompute collation tests (CPU-only, no kernel needed). */
  printf("Running batched-precompute collation tests... "); fflush(stdout);
  if (!test_precompute_collate()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Markov tests (CPU-only, no kernel needed). */
  printf("Running Markov tests... "); fflush(stdout);
  if (!test_markov()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Mask parser tests (CPU-only, no kernel needed). */
  printf("Running Mask parser tests... "); fflush(stdout);
  if (!test_mask_parse()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  /* Golden-vector tests (CPU-only, no kernel needed).
   * Pins the canonical math for ntlm_hash, md5_hash, netntlmv1_hash,
   * hash_to_index, and index_to_plaintext so backend drift is caught. */
  printf("Running golden vector tests... "); fflush(stdout);
  if (!test_golden()) {
    all_passed = 0;
    PRINT_FAILED();
  } else
    PRINT_PASSED();

  return all_passed;
}
