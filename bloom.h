/*
 * Rainbow Crackalack: bloom.h
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

#ifndef _BLOOM_H
#define _BLOOM_H

#include <stdint.h>
#include <stdatomic.h>

#define BLOOM_NUM_HASHES 7

typedef struct {
  uint8_t  *bits;
  uint64_t  num_bits;   /* Total bits (always power of 2) */
  uint64_t  mask;       /* num_bits - 1, for fast modulo */
  _Atomic uint64_t query_count;
  _Atomic uint64_t pass_count;
  _Atomic uint64_t confirmed_count;
} bloom_filter;

/* Allocate a bloom filter sized for num_elements with ~1% FPR. */
bloom_filter *bloom_create(uint64_t num_elements);

/* Insert a 64-bit key. NOT thread-safe. */
void bloom_insert(bloom_filter *bf, uint64_t key);

/* Returns 1 if key MIGHT be present, 0 if DEFINITELY absent.
 * Updates query_count/pass_count atomically.  Thread-safe for
 * concurrent reads after construction. */
int bloom_query(bloom_filter *bf, uint64_t key);

/* Record that a bloom_query hit was confirmed by the binary search.
 * Updates confirmed_count atomically.  Thread-safe. */
void bloom_record_confirmed(bloom_filter *bf);

/* Read current counter and sizing snapshot.  May be called from any
 * thread; values are a relaxed-atomic snapshot. */
void bloom_get_stats(const bloom_filter *bf,
                     uint64_t *out_queries,
                     uint64_t *out_passes,
                     uint64_t *out_confirmed,
                     uint64_t *out_num_bits,
                     unsigned int *out_num_hashes);

/* Free the bloom filter. */
void bloom_free(bloom_filter *bf);

#endif /* _BLOOM_H */
