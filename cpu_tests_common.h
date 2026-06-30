/*
 * Rainbow Crackalack: cpu_tests_common.h
 *
 * Shared declaration for the CPU-only unit test suite.  Implemented in
 * cpu_tests_common.c and called from both crackalack_cpu_tests (no-GPU
 * standalone binary) and crackalack_unit_tests (full GPU+CPU suite).
 */

#ifndef _CPU_TESTS_COMMON_H
#define _CPU_TESTS_COMMON_H

/* Run all CPU-only unit tests (no GPU required).
 *
 * Returns 1 if every test passed, 0 if any test failed.
 * Individual failures are printed to stdout as FAILED! lines. */
int run_cpu_only_tests(void);

#endif /* _CPU_TESTS_COMMON_H */
