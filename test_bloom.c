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


/* Counters must increment on bloom_query: query_count always,
 * pass_count only when the bloom reports present. */
static int test_bloom_counters(void) {
  bloom_filter *bf = bloom_create(1000);
  if (!bf) return 0;

  for (uint64_t i = 0; i < 100; i++) bloom_insert(bf, i + 1);

  /* Hit case: should bump both counters. */
  for (uint64_t i = 0; i < 100; i++) (void)bloom_query(bf, i + 1);

  /* Probably-miss case: 100 keys we never inserted.  query_count
   * always bumps; pass_count bumps only on false positives. */
  for (uint64_t i = 100; i < 200; i++) (void)bloom_query(bf, i + 1);

  uint64_t q = 0, p = 0, c = 0, nbits = 0;
  unsigned int nhash = 0;
  bloom_get_stats(bf, &q, &p, &c, &nbits, &nhash);
  bloom_free(bf);

  if (q != 200) { printf("query_count=%llu, expected 200\n", (unsigned long long)q); return 0; }
  if (p < 100)  { printf("pass_count=%llu, expected >= 100\n", (unsigned long long)p); return 0; }
  if (p > 110)  { printf("pass_count=%llu, expected <= 110 (too many false positives)\n", (unsigned long long)p); return 0; }
  if (c != 0)   { printf("confirmed_count=%llu, expected 0 (no record calls)\n", (unsigned long long)c); return 0; }
  if (nbits == 0 || nhash == 0) { printf("stats: bits=%llu hashes=%u\n", (unsigned long long)nbits, nhash); return 0; }
  return 1;
}


int test_bloom(void) {
  if (!test_bloom_roundtrip()) return 0;
  if (!test_bloom_counters()) return 0;
  return 1;
}
