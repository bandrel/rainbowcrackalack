/*
 * Rainbow Crackalack: test_bloom.c
 *
 * Pure-C unit tests for bloom.c.  No GPU dependency.
 */

#include <stdio.h>
#include <stdint.h>
#include "bloom.h"
#include "test_bloom.h"


/* Basic round-trip: insert N keys, query each one, expect 1 (present).
 * Also query N keys that were never inserted, expect mostly 0; we
 * tolerate up to 5% bloom false positives at this sizing (legacy API
 * targets ~1%). */
static int test_bloom_roundtrip(void) {
  const uint64_t n = 10000;
  bloom_filter *bf = bloom_create(n);
  if (!bf) { printf("bloom_create returned NULL\n"); return 0; }

  for (uint64_t i = 0; i < n; i++)
    bloom_insert(bf, i * 0x9E3779B97F4A7C15ULL + 1);

  for (uint64_t i = 0; i < n; i++) {
    if (!bloom_query(bf, i * 0x9E3779B97F4A7C15ULL + 1)) {
      printf("bloom_query reported FALSE NEGATIVE at i=%llu\n",
             (unsigned long long)i);
      bloom_free(bf);
      return 0;
    }
  }

  uint64_t false_positives = 0;
  for (uint64_t i = 0; i < n; i++)
    if (bloom_query(bf, (i ^ 0xDEADBEEFCAFEBABEULL) + 1))
      false_positives++;

  bloom_free(bf);
  if (false_positives > (n / 20)) {
    printf("bloom: too many false positives (%llu / %llu)\n",
           (unsigned long long)false_positives, (unsigned long long)n);
    return 0;
  }
  return 1;
}


int test_bloom(void) {
  if (!test_bloom_roundtrip()) return 0;
  return 1;
}
