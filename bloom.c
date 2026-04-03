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


bloom_filter *bloom_create(uint64_t num_elements) {
  bloom_filter *bf = calloc(1, sizeof(bloom_filter));
  if (!bf)
    return NULL;

  /* ~10 bits per element with 7 hash functions gives ~1% FPR. */
  uint64_t raw_bits = (uint64_t)((double)num_elements * 9.585) + 64;
  bf->num_bits = next_pow2(raw_bits);
  bf->mask = bf->num_bits - 1;

  uint64_t num_bytes = bf->num_bits / 8;
  bf->bits = calloc(num_bytes, 1);
  if (!bf->bits) {
    free(bf);
    return NULL;
  }

  return bf;
}


void bloom_insert(bloom_filter *bf, uint64_t key) {
  uint64_t mixed = mix64(key);
  uint32_t h1 = (uint32_t)mixed;
  uint32_t h2 = (uint32_t)(mixed >> 32) | 1;  /* force odd for coprime stride */

  for (int i = 0; i < BLOOM_NUM_HASHES; i++) {
    uint64_t pos = (h1 + (uint64_t)i * h2) & bf->mask;
    bf->bits[pos >> 3] |= (uint8_t)(1 << (pos & 7));
  }
}


int bloom_query(const bloom_filter *bf, uint64_t key) {
  uint64_t mixed = mix64(key);
  uint32_t h1 = (uint32_t)mixed;
  uint32_t h2 = (uint32_t)(mixed >> 32) | 1;

  for (int i = 0; i < BLOOM_NUM_HASHES; i++) {
    uint64_t pos = (h1 + (uint64_t)i * h2) & bf->mask;
    if (!(bf->bits[pos >> 3] & (1 << (pos & 7))))
      return 0;
  }
  return 1;
}


void bloom_free(bloom_filter *bf) {
  if (bf) {
    free(bf->bits);
    free(bf);
  }
}
