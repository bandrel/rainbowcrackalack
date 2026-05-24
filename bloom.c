/*
 * Rainbow Crackalack: bloom.c
 * Copyright (C) 2018-2020  Joe Testa <jtesta@positronsecurity.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bloom.h"


/* splitmix64 finalizer -- ensures good bit distribution from any input. */
static inline uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}


static inline uint64_t next_pow2(uint64_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v |= v >> 32;
  return v + 1;
}


bloom_filter *bloom_create(uint64_t num_elements, double target_fpr) {
  if (num_elements == 0)                 return NULL;
  if (target_fpr <= 0.0 || target_fpr >= 1.0) {
    if (target_fpr != 0.0)
      fprintf(stderr, "bloom_create: invalid target_fpr=%g; bloom disabled.\n", target_fpr);
    return NULL;
  }

  /* m_raw = -n * ln(p) / (ln 2)^2 */
  const double LN2 = 0.6931471805599453;
  double m_raw = -((double)num_elements) * log(target_fpr) / (LN2 * LN2);
  if (m_raw < 64.0) m_raw = 64.0;
  uint64_t m_pow2 = next_pow2((uint64_t)m_raw);

  /* Hard cap: 64 G bits = 8 GB. */
  if (m_pow2 > (1ULL << 36)) {
    fprintf(stderr, "bloom_create: refusing to allocate %.2f GB bloom (n=%llu, fpr=%g)\n",
            (double)m_pow2 / (8.0 * 1024.0 * 1024.0 * 1024.0),
            (unsigned long long)num_elements, target_fpr);
    return NULL;
  }

  /* k = round((m/n) * ln 2), clamped to [1, 64]. */
  double k_raw = ((double)m_pow2 / (double)num_elements) * LN2;
  int k = (int)(k_raw + 0.5);
  if (k < 1)  k = 1;
  if (k > 64) k = 64;

  bloom_filter *bf = calloc(1, sizeof(bloom_filter));
  if (!bf) return NULL;
  bf->num_bits   = m_pow2;
  bf->mask       = m_pow2 - 1;
  bf->num_hashes = (unsigned int)k;
  bf->bits       = calloc(m_pow2 / 8, 1);
  if (!bf->bits) { free(bf); return NULL; }
  return bf;
}


void bloom_insert(bloom_filter *bf, uint64_t key) {
  uint64_t mixed = mix64(key);
  uint32_t h1 = (uint32_t)mixed;
  uint32_t h2 = (uint32_t)(mixed >> 32) | 1;  /* force odd for coprime stride */

  for (unsigned int i = 0; i < bf->num_hashes; i++) {
    uint64_t pos = (h1 + (uint64_t)i * h2) & bf->mask;
    bf->bits[pos >> 3] |= (uint8_t)(1 << (pos & 7));
  }
}


int bloom_query(bloom_filter *bf, uint64_t key) {
  atomic_fetch_add_explicit(&bf->query_count, 1, memory_order_relaxed);

  uint64_t mixed = mix64(key);
  uint32_t h1 = (uint32_t)mixed;
  uint32_t h2 = (uint32_t)(mixed >> 32) | 1;

  for (unsigned int i = 0; i < bf->num_hashes; i++) {
    uint64_t pos = (h1 + (uint64_t)i * h2) & bf->mask;
    if (!(bf->bits[pos >> 3] & (1 << (pos & 7))))
      return 0;
  }
  atomic_fetch_add_explicit(&bf->pass_count, 1, memory_order_relaxed);
  return 1;
}


void bloom_record_confirmed(bloom_filter *bf) {
  atomic_fetch_add_explicit(&bf->confirmed_count, 1, memory_order_relaxed);
}


void bloom_get_stats(const bloom_filter *bf,
                     uint64_t *out_queries,
                     uint64_t *out_passes,
                     uint64_t *out_confirmed,
                     uint64_t *out_num_bits,
                     unsigned int *out_num_hashes) {
  /* atomic_load on a const-cast pointer is well-defined for _Atomic
   * fields; the cast is purely to satisfy the const declaration of
   * the parameter. */
  bloom_filter *mbf = (bloom_filter *)bf;
  if (out_queries)   *out_queries   = atomic_load_explicit(&mbf->query_count,     memory_order_relaxed);
  if (out_passes)    *out_passes    = atomic_load_explicit(&mbf->pass_count,      memory_order_relaxed);
  if (out_confirmed) *out_confirmed = atomic_load_explicit(&mbf->confirmed_count, memory_order_relaxed);
  if (out_num_bits)  *out_num_bits  = bf->num_bits;
  if (out_num_hashes) *out_num_hashes = bf->num_hashes;
}


void bloom_free(bloom_filter *bf) {
  if (bf) {
    free(bf->bits);
    free(bf);
  }
}
