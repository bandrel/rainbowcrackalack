/*
 * Rainbow Crackalack: test_precompute_collate.c
 *
 * CPU-only known-value tests for collate_batched_precompute_endpoints().
 *
 * Regression target: the NetNTLMv1/NTLM8 batched multi-hash lookup bug where
 * the second (and every) hash in a >=2-hash batch was silently never found.
 * Root cause was this exact remap: the batched precompute emits endpoints in
 * REVERSE chain-column order, and the buggy code wrote them straight through
 * (out[p] = hash_output[p]) instead of remapping to column order
 * (out[positions_per_hash - 2 - p] = hash_output[p]).  The false-alarm kernel
 * walks each candidate chain to a depth equal to the endpoint's ARRAY INDEX, so
 * a mirrored index made the walk stop at the wrong depth and drop the crack.
 *
 * These tests assert the column-aligned layout with known values, so a
 * straight-through (or any other mis-indexed) implementation fails here long
 * before it reaches a GPU.
 */
#include <stdio.h>
#include <stdint.h>

#include "precompute_collate.h"
#include "test_precompute_collate.h"

#define SENT PRECOMPUTE_COLLATE_SENTINEL

/* Group A: single endpoint lands at its true chain column (not its mirror).
 *
 * With positions_per_hash = 10, source index p maps to column (8 - p).  We use
 * p = 1 -> column 7 (note 1 != 7, so a straight-through copy would put it at
 * index 1 and this test would fail).  Every other slot must be the sentinel,
 * including the final column (index 9), which has no source. */
static int group_a(void) {
  int ok = 1;
  const unsigned int pph = 10;
  uint64_t in[10] = {0};
  uint64_t out[10];
  const uint64_t ENDPOINT = 0x00112233445566ULL; /* < 2^56, like a real endpoint */

  in[1] = ENDPOINT;                  /* column 8 - 1 = 7 */
  collate_batched_precompute_endpoints(in, pph, out);

  if (out[7] != ENDPOINT) {
    fprintf(stderr, "COLLATE-A01 failed: out[7]=%llu, expected endpoint %llu\n",
            (unsigned long long)out[7], (unsigned long long)ENDPOINT);
    ok = 0;
  }
  /* The mirrored index (where the buggy straight-through copy would put it). */
  if (out[1] == ENDPOINT) {
    fprintf(stderr, "COLLATE-A02 failed: endpoint at mirrored index 1 "
            "(straight-through bug)\n");
    ok = 0;
  }
  for (unsigned int i = 0; i < pph; i++) {
    if (i == 7) continue;
    if (out[i] != SENT) {
      fprintf(stderr, "COLLATE-A03 failed: out[%u]=%llu, expected sentinel\n",
              i, (unsigned long long)out[i]);
      ok = 0;
    }
  }
  return ok;
}

/* Group B: multiple endpoints, full column-order mapping.
 * p -> column (8 - p): p0->c8, p2->c6, p8->c0.  Column 9 has no source. */
static int group_b(void) {
  int ok = 1;
  const unsigned int pph = 10;
  uint64_t in[10] = {0};
  uint64_t out[10];

  in[0] = 0xAAAA; /* column 8 */
  in[2] = 0xBBBB; /* column 6 */
  in[8] = 0xCCCC; /* column 0 */
  collate_batched_precompute_endpoints(in, pph, out);

  uint64_t expect[10] = {
    0xCCCC, SENT, SENT, SENT, SENT, SENT, 0xBBBB, SENT, 0xAAAA, SENT
  };
  for (unsigned int i = 0; i < pph; i++) {
    if (out[i] != expect[i]) {
      fprintf(stderr, "COLLATE-B01 failed: out[%u]=%llu, expected %llu\n",
              i, (unsigned long long)out[i], (unsigned long long)expect[i]);
      ok = 0;
    }
  }
  return ok;
}

/* Group C: the actual multi-hash regression -- "second hash always not found".
 *
 * Simulate the batched all_output buffer for TWO hashes laid out back-to-back
 * (num_hashes x positions_per_hash), collate each hash's slice independently
 * (exactly as batch_precompute_all_hashes does), and assert the SECOND hash's
 * endpoint is recovered at its true column.  Under the old straight-through
 * code the second hash's endpoint landed at a mirrored index and the crack was
 * dropped -- this asserts it is not. */
static int group_c(void) {
  int ok = 1;
  const unsigned int pph = 8;            /* columns 0..7; sources p in [0,6] */
  const unsigned int num_hashes = 2;
  uint64_t all_output[16] = {0};         /* num_hashes * pph */
  uint64_t out[8];

  /* Hash 0: endpoint at column 5 (p = 8 - ... ; pph-2-p=5 -> p=1). */
  all_output[0 * pph + 1] = 0xDEAD;      /* hash 0, column 6 - 1 = 5 */
  /* Hash 1: endpoint at column 2 (pph-2-p=2 -> p = pph-4 = 4). */
  all_output[1 * pph + 4] = 0xBEEF;      /* hash 1, column 6 - 4 = 2 */

  for (unsigned int h = 0; h < num_hashes; h++) {
    const uint64_t *slice = all_output + (size_t)h * pph;
    collate_batched_precompute_endpoints(slice, pph, out);

    if (h == 0) {
      if (out[5] != 0xDEAD) {
        fprintf(stderr, "COLLATE-C01 failed: hash0 out[5]=%llu, expected 0xDEAD\n",
                (unsigned long long)out[5]);
        ok = 0;
      }
    } else {
      /* This is the crux: the second hash must be found at column 2. */
      if (out[2] != 0xBEEF) {
        fprintf(stderr, "COLLATE-C02 failed: SECOND hash dropped -- out[2]=%llu, "
                "expected 0xBEEF (multi-hash regression)\n",
                (unsigned long long)out[2]);
        ok = 0;
      }
    }
  }
  return ok;
}

/* Group D: degenerate sizes don't read/write out of bounds and stay all-sentinel
 * when there is no valid source column. */
static int group_d(void) {
  int ok = 1;
  uint64_t in1[1] = { 0x1234 };
  uint64_t out1[1] = { 0 };
  uint64_t in2[2] = { 0x1234, 0x5678 };
  uint64_t out2[2] = { 0, 0 };

  /* pph=1: no column has a source (p+2<=1 never true) -> single sentinel. */
  collate_batched_precompute_endpoints(in1, 1, out1);
  if (out1[0] != SENT) {
    fprintf(stderr, "COLLATE-D01 failed: pph=1 out[0]=%llu, expected sentinel\n",
            (unsigned long long)out1[0]);
    ok = 0;
  }
  /* pph=2: only p=0 -> column 0; column 1 is the final (no source) sentinel. */
  collate_batched_precompute_endpoints(in2, 2, out2);
  if (out2[0] != 0x1234 || out2[1] != SENT) {
    fprintf(stderr, "COLLATE-D02 failed: pph=2 out={%llu,%llu}, expected {0x1234,SENT}\n",
            (unsigned long long)out2[0], (unsigned long long)out2[1]);
    ok = 0;
  }
  return ok;
}

int test_precompute_collate(void) {
  int ok = 1;
  if (!group_a()) ok = 0;
  if (!group_b()) ok = 0;
  if (!group_c()) ok = 0;
  if (!group_d()) ok = 0;
  return ok;
}
