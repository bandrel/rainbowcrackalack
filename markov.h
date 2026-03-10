/*
 * Rainbow Crackalack: markov.h
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

#ifndef MARKOV_H
#define MARKOV_H

#include <stdint.h>

#define MARKOV_MAGIC "RCLM"
#define MARKOV_VERSION 2

/* In-memory Markov model after loading/training. */
typedef struct {
  unsigned int  charset_len;
  char          charset[256];       /* the charset characters in order */
  uint64_t     *pos0_freq;          /* [charset_len] - position-0 counts */
  uint64_t     *bigram_freq;        /* [charset_len * charset_len] - bigram counts */
  /* Sorted lookup tables for GPU (built after load/train) */
  uint8_t      *sorted_pos0;        /* [charset_len] - char indices sorted by freq desc (max charset_len 255) */
  uint8_t      *sorted_bigram;      /* [charset_len * charset_len] - per-prev sorted indices (max charset_len 255) */
} markov_model;

/* Train a model from a wordlist file.
 * charset must be exactly the characters allowed (e.g. ascii-32-95 string).
 * Returns 0 on success, -1 on error (with message to stderr). */
int markov_train(const char *wordlist_path, const char *charset,
                 unsigned int charset_len, markov_model *model);

/* Write model to .markov file. Returns 0 on success, -1 on error. */
int markov_save(const char *path, const markov_model *model);

/* Load model from .markov file and build sorted tables.
 * Caller must call markov_free() when done.
 * Returns 0 on success, -1 on error. */
int markov_load(const char *path, markov_model *model);

/* Build sorted lookup tables from freq counts (call after train or load). */
void markov_build_sorted(markov_model *model);

/* CPU reference: map index -> plaintext using bigram probability order.
 * index 0 = most probable plaintext.
 * plaintext must be at least plaintext_len bytes. */
void index_to_plaintext_markov_cpu(uint64_t index, const markov_model *model,
                                   unsigned int plaintext_len,
                                   unsigned char *plaintext);

/* Free all heap memory in model. */
void markov_free(markov_model *model);

#endif /* MARKOV_H */
