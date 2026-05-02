#include <metal_stdlib>
using namespace metal;

#include "shared.h"

/* SplitMix64 — matches bloom.c exactly. */
static inline ulong splitmix64(ulong x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

/* Query bloom filter stored as ulong array. */
static inline int bloom_query_vram(device ulong *bf_bits, uint64_t bf_num_bits, uint64_t bf_mask, ulong key) {
  ulong x = splitmix64(key);
  uint h1 = (uint)x;
  uint h2 = (uint)(x >> 32) | 1;

  for (int i = 0; i < BLOOM_NUM_HASHES; i++) {
    ulong pos = ((ulong)h1 + (ulong)i * h2) & bf_mask;
    uint byte_idx = (uint)(pos >> 3);
    uint bit_idx = (uint)(pos & 7);
    ulong word_idx = byte_idx >> 6;
    ulong bit_in_word = bit_idx;
    if (!(bf_bits[word_idx] & ((ulong)1 << bit_in_word)))
      return 0;
  }
  return 1;
}

/*
 * GPU-accelerated binary search (Metal).
 * Same algorithm as gpu_binary_search.cl but with Metal semantics.
 */
kernel void gpu_binary_search(
    device ulong *rainbow_table,
    device ulong *bf_bits,
    constant uint64_t &bf_num_bits,
    constant uint64_t &bf_mask,
    device ulong *precomputed_end_indices,
    constant uint32_t &num_end_indices,
    device uint32_t *results_counts,
    device ulong *results_chains,
    uint32_t gid [[thread_position_in_grid]])
{
  if (gid >= num_end_indices) return;

  ulong search_index = precomputed_end_indices[gid];

  if (!bloom_query_vram(bf_bits, bf_num_bits, bf_mask, search_index))
    return;

  uint32_t low = 0, high = (uint32_t)num_chains;

  while (high - low > 16) {
    uint32_t mid = ((high - low) / 2) + low;
    if (search_index >= rainbow_table[(mid * 2) + 1])
      low = mid;
    else
      high = mid;
  }

  uint32_t chain = low;
  uint32_t remaining = high - low;

  while (remaining >= 4) {
    ulong e0 = rainbow_table[(chain * 2) + 1];
    ulong e1 = rainbow_table[(((uint32_t)chain + 1) * 2) + 1];
    ulong e2 = rainbow_table[(((uint32_t)chain + 2) * 2) + 1];
    ulong e3 = rainbow_table[(((uint32_t)chain + 3) * 2) + 1];

    if (e0 == search_index) {
      uint32_t count = atomic_fetch_add(&results_counts[gid], 1u);
      results_chains[(ulong)gid * 65536 + count * 2 + 0] = chain;
      results_chains[(ulong)gid * 65536 + count * 2 + 1] = gid;
    }
    if (e1 == search_index) {
      uint32_t count = atomic_fetch_add(&results_counts[gid], 1u);
      results_chains[(ulong)gid * 65536 + count * 2 + 0] = chain + 1;
      results_chains[(ulong)gid * 65536 + count * 2 + 1] = gid;
    }
    if (e2 == search_index) {
      uint32_t count = atomic_fetch_add(&results_counts[gid], 1u);
      results_chains[(ulong)gid * 65536 + count * 2 + 0] = chain + 2;
      results_chains[(ulong)gid * 65536 + count * 2 + 1] = gid;
    }
    if (e3 == search_index) {
      uint32_t count = atomic_fetch_add(&results_counts[gid], 1u);
      results_chains[(ulong)gid * 65536 + count * 2 + 0] = chain + 3;
      results_chains[(ulong)gid * 65536 + count * 2 + 1] = gid;
    }

    chain += 4;
    remaining -= 4;
  }

  while (chain < high) {
    if (search_index == rainbow_table[(chain * 2) + 1]) {
      uint32_t count = atomic_fetch_add(&results_counts[gid], 1u);
      results_chains[(ulong)gid * 65536 + count * 2 + 0] = chain;
      results_chains[(ulong)gid * 65536 + count * 2 + 1] = gid;
    }
    chain++;
  }
}
