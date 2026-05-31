/*
 * gpu_binary_search.cu — GPU-accelerated rainbow-table endpoint binary search.
 *
 * Each thread handles one packed precomputed end index, optionally pre-filters
 * via a bloom check, runs a binary search over the rainbow-table endpoints,
 * and (on match) atomically appends a (chain_index, entry_index) pair into a
 * single shared output buffer.  The host re-uses entry_index to map back to
 * the originating ppi / position slot.
 *
 * Args (all `gpu_buffer` from the host -- scalars come in via single-element
 * device buffers in the existing CUDA convention):
 *   g_rainbow_table        unsigned long long *, num_chains * 2 entries
 *                          [start_0, end_0, start_1, end_1, ...]
 *   g_num_chains           unsigned long long *, single value
 *   g_bf_bits              unsigned char *      , bloom byte array (8 bits/byte)
 *   g_bf_mask              unsigned long long * , bloom mask (num_bits - 1)
 *   g_bf_num_hashes        unsigned int *       , k (number of bloom hashes)
 *                          0 disables bloom (no prefilter)
 *   g_end_indices          unsigned long long *, total_end_indices entries
 *   g_total_end_indices    unsigned int *       , single value
 *   g_out_cap              unsigned int *       , max number of match pairs
 *                          the output buffer can hold (capacity in *pairs*)
 *   g_out_head             unsigned int *       , single atomic counter
 *                          (host pre-zeroes; kernel atomicAdds)
 *   g_out_results          unsigned long long *, out_cap * 2 entries
 *                          (chain_index, entry_index, chain_index, ...)
 */

/* SplitMix64 -- matches bloom.c's mix64() exactly. */
__device__ static inline unsigned long long bsearch_splitmix64(unsigned long long x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

/* Query bloom filter -- matches bloom.c's bloom_query() bit layout exactly
 * (byte array, bit indexed within byte by `pos & 7`).
 *
 * Returns 1 if key MIGHT be present, 0 if DEFINITELY absent. */
__device__ static inline int bsearch_bloom_query(const unsigned char *bf_bits,
                                                 unsigned long long bf_mask,
                                                 unsigned int bf_num_hashes,
                                                 unsigned long long key) {
  unsigned long long mixed = bsearch_splitmix64(key);
  unsigned int h1 = (unsigned int)mixed;
  unsigned int h2 = (unsigned int)(mixed >> 32) | 1u;  /* odd stride */

  for (unsigned int i = 0; i < bf_num_hashes; i++) {
    unsigned long long pos = ((unsigned long long)h1 + (unsigned long long)i * (unsigned long long)h2) & bf_mask;
    if (!(bf_bits[pos >> 3] & (1u << (pos & 7u))))
      return 0;
  }
  return 1;
}

/* Binary-search the endpoint column for `search_index` and append every match
 * to the shared output buffer.  Mirrors _rt_binary_search() in
 * crackalack_lookup.c (same low/high refinement, same 4-wide unrolled tail). */
__device__ static inline void bsearch_emit_matches(const unsigned long long *rainbow_table,
                                                   unsigned long long num_chains,
                                                   unsigned long long search_index,
                                                   unsigned int entry_index,
                                                   unsigned int *out_head,
                                                   unsigned int out_cap,
                                                   unsigned long long *out_results) {
  unsigned long long low = 0, high = num_chains;

  while (high - low > 16ULL) {
    unsigned long long mid = ((high - low) / 2ULL) + low;
    if (search_index >= rainbow_table[(mid * 2ULL) + 1ULL])
      low = mid;
    else
      high = mid;
  }

  unsigned long long chain = low;
  unsigned long long remaining = high - low;

  while (remaining >= 4ULL) {
    unsigned long long e0 = rainbow_table[(chain * 2ULL) + 1ULL];
    unsigned long long e1 = rainbow_table[((chain + 1ULL) * 2ULL) + 1ULL];
    unsigned long long e2 = rainbow_table[((chain + 2ULL) * 2ULL) + 1ULL];
    unsigned long long e3 = rainbow_table[((chain + 3ULL) * 2ULL) + 1ULL];

    if (e0 == search_index) {
      unsigned int slot = atomicAdd(out_head, 1u);
      if (slot < out_cap) {
        out_results[(unsigned long long)slot * 2ULL + 0ULL] = chain;
        out_results[(unsigned long long)slot * 2ULL + 1ULL] = entry_index;
      }
    }
    if (e1 == search_index) {
      unsigned int slot = atomicAdd(out_head, 1u);
      if (slot < out_cap) {
        out_results[(unsigned long long)slot * 2ULL + 0ULL] = chain + 1ULL;
        out_results[(unsigned long long)slot * 2ULL + 1ULL] = entry_index;
      }
    }
    if (e2 == search_index) {
      unsigned int slot = atomicAdd(out_head, 1u);
      if (slot < out_cap) {
        out_results[(unsigned long long)slot * 2ULL + 0ULL] = chain + 2ULL;
        out_results[(unsigned long long)slot * 2ULL + 1ULL] = entry_index;
      }
    }
    if (e3 == search_index) {
      unsigned int slot = atomicAdd(out_head, 1u);
      if (slot < out_cap) {
        out_results[(unsigned long long)slot * 2ULL + 0ULL] = chain + 3ULL;
        out_results[(unsigned long long)slot * 2ULL + 1ULL] = entry_index;
      }
    }

    chain += 4ULL;
    remaining -= 4ULL;
  }

  while (chain < high) {
    if (search_index == rainbow_table[(chain * 2ULL) + 1ULL]) {
      unsigned int slot = atomicAdd(out_head, 1u);
      if (slot < out_cap) {
        out_results[(unsigned long long)slot * 2ULL + 0ULL] = chain;
        out_results[(unsigned long long)slot * 2ULL + 1ULL] = entry_index;
      }
    }
    chain++;
  }
}


extern "C" __global__ void gpu_binary_search(
    unsigned long long *g_rainbow_table,
    unsigned long long *g_num_chains,
    unsigned char      *g_bf_bits,
    unsigned long long *g_bf_mask,
    unsigned int       *g_bf_num_hashes,
    unsigned long long *g_end_indices,
    unsigned int       *g_total_end_indices,
    unsigned int       *g_out_cap,
    unsigned int       *g_out_head,
    unsigned long long *g_out_results) {

  unsigned int gid = blockIdx.x * blockDim.x + threadIdx.x;
  unsigned int total = *g_total_end_indices;
  if (gid >= total) return;

  unsigned long long search_index = g_end_indices[gid];

  /* Bloom prefilter (when k > 0 -- 0 means caller disabled the bloom). */
  unsigned int bf_num_hashes = *g_bf_num_hashes;
  if (bf_num_hashes > 0u) {
    if (!bsearch_bloom_query(g_bf_bits, *g_bf_mask, bf_num_hashes, search_index))
      return;
  }

  bsearch_emit_matches(g_rainbow_table, *g_num_chains, search_index, gid,
                       g_out_head, *g_out_cap, g_out_results);
}
