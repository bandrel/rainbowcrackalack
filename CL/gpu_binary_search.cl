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

/* Query bloom filter stored as ulong array (64 bits per word). */
static inline int bloom_query_vram(__global ulong *bf_bits, uint64_t bf_num_bits, uint64_t bf_mask, ulong key) {
  ulong x = splitmix64(key);
  uint h1 = (uint)x;
  uint h2 = (uint)(x >> 32) | 1;

  for (int i = 0; i < BLOOM_NUM_HASHES; i++) {
    ulong pos = ((ulong)h1 + (ulong)i * h2) & bf_mask;
    uint byte_idx = (uint)(pos >> 3);
    uint bit_idx = (uint)(pos & 7);
    ulong word_idx = byte_idx >> 6;  /* / 64 */
    ulong bit_in_word = bit_idx;     /* bit within the 64-bit word */
    if (!(bf_bits[word_idx] & ((ulong)1 << bit_in_word)))
      return 0;
  }
  return 1;
}

/*
 * GPU-accelerated binary search.
 *
 * Each work-item handles one precomputed_end_indices entry.
 * Outputs: per-end-index chain matches into results_chains[].
 *
 * Output layout per match (4 ulong values):
 *   results_chains[total_matches + 0] = chain_index
 *   results_chains[total_matches + 1] = match_count_for_this_entry (before this match)
 *   results_chains[total_matches + 2] = entry_index (which precomputed_end_indices slot)
 *
 * results_counts[entry_index] holds the total match count for that entry.
 */
__kernel void gpu_binary_search(
    __global ulong *rainbow_table,    /* [num_chains * 2]: [start, end, start, end, ...] */
    __global ulong *bf_bits,
    uint64_t bf_num_bits,
    uint64_t bf_mask,
    __global ulong *precomputed_end_indices,
    uint32_t num_end_indices,
    __global uint32_t *results_counts,
    __global ulong *results_chains)
{
  uint32_t gid = get_global_id(0);
  if (gid >= num_end_indices) return;

  ulong search_index = precomputed_end_indices[gid];

  /* Bloom filter pre-filter: if bloom says "absent", skip binary search. */
  if (!bloom_query_vram(bf_bits, bf_num_bits, bf_mask, search_index))
    return;

  /* Binary search over end indices (matches _rt_binary_search in CPU code). */
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

  /* Unrolled chunk: check 4 at a time. */
  while (remaining >= 4) {
    ulong e0 = rainbow_table[(chain * 2) + 1];
    ulong e1 = rainbow_table[(((uint32_t)chain + 1) * 2) + 1];
    ulong e2 = rainbow_table[(((uint32_t)chain + 2) * 2) + 1];
    ulong e3 = rainbow_table[(((uint32_t)chain + 3) * 2) + 1];

    if (e0 == search_index) {
      uint32_t count = atomic_add(&results_counts[gid], 1);
      results_chains[(uint64_t)gid * 65536 + count * 2 + 0] = chain;
      results_chains[(uint64_t)gid * 65536 + count * 2 + 1] = gid;  /* position marker */
    }
    if (e1 == search_index) {
      uint32_t count = atomic_add(&results_counts[gid], 1);
      results_chains[(uint64_t)gid * 65536 + count * 2 + 0] = chain + 1;
      results_chains[(uint64_t)gid * 65536 + count * 2 + 1] = gid;
    }
    if (e2 == search_index) {
      uint32_t count = atomic_add(&results_counts[gid], 1);
      results_chains[(uint64_t)gid * 65536 + count * 2 + 0] = chain + 2;
      results_chains[(uint64_t)gid * 65536 + count * 2 + 1] = gid;
    }
    if (e3 == search_index) {
      uint32_t count = atomic_add(&results_counts[gid], 1);
      results_chains[(uint64_t)gid * 65536 + count * 2 + 0] = chain + 3;
      results_chains[(uint64_t)gid * 65536 + count * 2 + 1] = gid;
    }

    chain += 4;
    remaining -= 4;
  }

  /* Final linear scan. */
  while (chain < high) {
    if (search_index == rainbow_table[(chain * 2) + 1]) {
      uint32_t count = atomic_add(&results_counts[gid], 1);
      results_chains[(uint64_t)gid * 65536 + count * 2 + 0] = chain;
      results_chains[(uint64_t)gid * 65536 + count * 2 + 1] = gid;
    }
    chain++;
  }
}
