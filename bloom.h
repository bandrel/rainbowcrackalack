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

#define BLOOM_NUM_HASHES 7

typedef struct {
  uint8_t  *bits;
  uint64_t  num_bits;   /* Total bits (always power of 2) */
  uint64_t  mask;       /* num_bits - 1, for fast modulo */
} bloom_filter;

/* Allocate a bloom filter sized for num_elements with ~1% FPR.
 * Returns NULL on allocation failure. */
bloom_filter *bloom_create(uint64_t num_elements);

/* Insert a 64-bit key. NOT thread-safe. */
void bloom_insert(bloom_filter *bf, uint64_t key);

/* Returns 1 if key MIGHT be present, 0 if DEFINITELY absent.
 * Thread-safe for concurrent reads after construction. */
int bloom_query(const bloom_filter *bf, uint64_t key);

/* Free the bloom filter. */
void bloom_free(bloom_filter *bf);

#endif /* _BLOOM_H */
