/*
 * gpu_binary_search.cl — OpenCL port of CUDA/gpu_binary_search.cu.
 * Same algorithm, same arg layout (scalars via single-element buffers).
 */

/* SplitMix64 -- matches bloom.c's mix64() exactly. */
static inline ulong bsearch_splitmix64(ulong x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9UL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebUL;
  x ^= x >> 31;
  return x;
}

/* Query bloom filter (byte array, bit indexed by `pos & 7`).
 * Returns 1 if key MIGHT be present, 0 if DEFINITELY absent. */
static inline int bsearch_bloom_query(__global unsigned char *bf_bits,
                                       ulong bf_mask,
                                       uint bf_num_hashes,
                                       ulong key) {
  ulong mixed = bsearch_splitmix64(key);
  uint h1 = (uint)mixed;
  uint h2 = (uint)(mixed >> 32) | 1u;

  for (uint i = 0; i < bf_num_hashes; i++) {
    ulong pos = ((ulong)h1 + (ulong)i * (ulong)h2) & bf_mask;
    if (!(bf_bits[pos >> 3] & (1u << (pos & 7u))))
      return 0;
  }
  return 1;
}


__kernel void gpu_binary_search(
    __global ulong *g_rainbow_table,
    __global ulong *g_num_chains,
    __global unsigned char *g_bf_bits,
    __global ulong *g_bf_mask,
    __global uint  *g_bf_num_hashes,
    __global ulong *g_end_indices,
    __global uint  *g_total_end_indices,
    __global uint  *g_out_cap,
    __global volatile uint *g_out_head,
    __global ulong *g_out_results) {

  uint gid = get_global_id(0);
  uint total = *g_total_end_indices;
  if (gid >= total) return;

  ulong search_index = g_end_indices[gid];

  uint bf_num_hashes = *g_bf_num_hashes;
  if (bf_num_hashes > 0u) {
    if (!bsearch_bloom_query(g_bf_bits, *g_bf_mask, bf_num_hashes, search_index))
      return;
  }

  ulong num_chains = *g_num_chains;
  uint out_cap = *g_out_cap;

  ulong low = 0, high = num_chains;

  while (high - low > 16UL) {
    ulong mid = ((high - low) / 2UL) + low;
    if (search_index >= g_rainbow_table[(mid * 2UL) + 1UL])
      low = mid;
    else
      high = mid;
  }

  ulong chain = low;
  ulong remaining = high - low;

  while (remaining >= 4UL) {
    ulong e0 = g_rainbow_table[(chain * 2UL) + 1UL];
    ulong e1 = g_rainbow_table[((chain + 1UL) * 2UL) + 1UL];
    ulong e2 = g_rainbow_table[((chain + 2UL) * 2UL) + 1UL];
    ulong e3 = g_rainbow_table[((chain + 3UL) * 2UL) + 1UL];

    if (e0 == search_index) {
      uint slot = atomic_add(g_out_head, 1u);
      if (slot < out_cap) {
        g_out_results[(ulong)slot * 2UL + 0UL] = chain;
        g_out_results[(ulong)slot * 2UL + 1UL] = gid;
      }
    }
    if (e1 == search_index) {
      uint slot = atomic_add(g_out_head, 1u);
      if (slot < out_cap) {
        g_out_results[(ulong)slot * 2UL + 0UL] = chain + 1UL;
        g_out_results[(ulong)slot * 2UL + 1UL] = gid;
      }
    }
    if (e2 == search_index) {
      uint slot = atomic_add(g_out_head, 1u);
      if (slot < out_cap) {
        g_out_results[(ulong)slot * 2UL + 0UL] = chain + 2UL;
        g_out_results[(ulong)slot * 2UL + 1UL] = gid;
      }
    }
    if (e3 == search_index) {
      uint slot = atomic_add(g_out_head, 1u);
      if (slot < out_cap) {
        g_out_results[(ulong)slot * 2UL + 0UL] = chain + 3UL;
        g_out_results[(ulong)slot * 2UL + 1UL] = gid;
      }
    }

    chain += 4UL;
    remaining -= 4UL;
  }

  while (chain < high) {
    if (search_index == g_rainbow_table[(chain * 2UL) + 1UL]) {
      uint slot = atomic_add(g_out_head, 1u);
      if (slot < out_cap) {
        g_out_results[(ulong)slot * 2UL + 0UL] = chain;
        g_out_results[(ulong)slot * 2UL + 1UL] = gid;
      }
    }
    chain++;
  }
}
