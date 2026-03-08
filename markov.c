/*
 * Rainbow Crackalack: markov.c
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

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "markov.h"

/* -------------------------------------------------------------------------
 * qsort_r comparator helpers
 *
 * macOS:  qsort_r(base, nel, width, thunk, compar(thunk, a, b))
 * Linux:  qsort_r(base, nmemb, size, compar(a, b, arg), arg)
 * ------------------------------------------------------------------------- */

typedef struct {
  const uint32_t *freq;
} sort_ctx;

#ifdef __APPLE__

static int cmp_by_freq_desc_apple(void *thunk, const void *a, const void *b)
{
  const sort_ctx *ctx = (const sort_ctx *)thunk;
  uint8_t ia = *(const uint8_t *)a;
  uint8_t ib = *(const uint8_t *)b;
  if (ctx->freq[ib] > ctx->freq[ia])
    return 1;
  if (ctx->freq[ib] < ctx->freq[ia])
    return -1;
  return (int)ia - (int)ib;   /* stable tie-break by index */
}

#else /* Linux */

static int cmp_by_freq_desc_linux(const void *a, const void *b, void *arg)
{
  const sort_ctx *ctx = (const sort_ctx *)arg;
  uint8_t ia = *(const uint8_t *)a;
  uint8_t ib = *(const uint8_t *)b;
  if (ctx->freq[ib] > ctx->freq[ia])
    return 1;
  if (ctx->freq[ib] < ctx->freq[ia])
    return -1;
  return (int)ia - (int)ib;
}

#endif /* __APPLE__ */

/* -------------------------------------------------------------------------
 * markov_build_sorted
 * ------------------------------------------------------------------------- */

void markov_build_sorted(markov_model *model)
{
  unsigned int n = model->charset_len;

  /* Initialise index arrays 0..n-1 */
  for (unsigned int i = 0; i < n; i++)
    model->sorted_pos0[i] = (uint8_t)i;

  for (unsigned int i = 0; i < n * n; i++)
    model->sorted_bigram[i] = (uint8_t)(i % n);

#ifdef __APPLE__
  /* Sort sorted_pos0 by pos0_freq descending */
  sort_ctx ctx0 = { model->pos0_freq };
  qsort_r(model->sorted_pos0, n, sizeof(uint8_t), &ctx0,
          cmp_by_freq_desc_apple);

  /* For each previous character p, sort sorted_bigram[p*n .. p*n+n-1] */
  for (unsigned int p = 0; p < n; p++) {
    sort_ctx ctxb = { model->bigram_freq + (size_t)p * n };
    qsort_r(model->sorted_bigram + (size_t)p * n, n, sizeof(uint8_t),
            &ctxb, cmp_by_freq_desc_apple);
  }
#else
  sort_ctx ctx0 = { model->pos0_freq };
  qsort_r(model->sorted_pos0, n, sizeof(uint8_t),
          cmp_by_freq_desc_linux, &ctx0);

  for (unsigned int p = 0; p < n; p++) {
    sort_ctx ctxb = { model->bigram_freq + (size_t)p * n };
    qsort_r(model->sorted_bigram + (size_t)p * n, n, sizeof(uint8_t),
            cmp_by_freq_desc_linux, &ctxb);
  }
#endif
}

/* -------------------------------------------------------------------------
 * markov_train
 * ------------------------------------------------------------------------- */

int markov_train(const char *wordlist_path, const char *charset,
                 unsigned int charset_len, markov_model *model)
{
  if (charset_len == 0 || charset_len > 256) {
    fprintf(stderr, "markov_train: charset_len %u out of range [1, 256]\n",
            charset_len);
    return -1;
  }

  memset(model, 0, sizeof(*model));
  model->charset_len = charset_len;
  memcpy(model->charset, charset, charset_len);

  model->pos0_freq    = calloc(charset_len, sizeof(uint32_t));
  model->bigram_freq  = calloc((size_t)charset_len * charset_len,
                                sizeof(uint32_t));
  model->sorted_pos0  = calloc(charset_len, sizeof(uint8_t));
  model->sorted_bigram = calloc((size_t)charset_len * charset_len,
                                 sizeof(uint8_t));

  if (!model->pos0_freq || !model->bigram_freq ||
      !model->sorted_pos0 || !model->sorted_bigram) {
    fprintf(stderr, "markov_train: out of memory\n");
    markov_free(model);
    return -1;
  }

  /* Build reverse lookup: ASCII byte -> index in charset (-1 = not in set) */
  int rev[256];
  memset(rev, -1, sizeof(rev));
  for (unsigned int i = 0; i < charset_len; i++)
    rev[(unsigned char)charset[i]] = (int)i;

  FILE *fp = fopen(wordlist_path, "r");
  if (!fp) {
    fprintf(stderr, "markov_train: cannot open wordlist '%s'\n", wordlist_path);
    markov_free(model);
    return -1;
  }

  char buf[4096];
  uint64_t valid_words = 0;

  while (fgets(buf, sizeof(buf), fp)) {
    /* Strip trailing CR / LF */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      buf[--len] = '\0';
    }

    if (len == 0)
      continue;

    /* Skip words containing characters outside the charset */
    int ok = 1;
    for (size_t i = 0; i < len; i++) {
      if (rev[(unsigned char)buf[i]] < 0) {
        ok = 0;
        break;
      }
    }
    if (!ok)
      continue;

    valid_words++;

    /* Count position-0 frequency */
    model->pos0_freq[rev[(unsigned char)buf[0]]]++;

    /* Count bigrams */
    for (size_t i = 0; i + 1 < len; i++) {
      unsigned int prev = (unsigned int)rev[(unsigned char)buf[i]];
      unsigned int next = (unsigned int)rev[(unsigned char)buf[i + 1]];
      model->bigram_freq[(size_t)prev * charset_len + next]++;
    }
  }

  fclose(fp);

  if (valid_words == 0) {
    fprintf(stderr, "markov_train: no valid words found in '%s' "
            "(all words contained characters outside the charset)\n",
            wordlist_path);
    markov_free(model);
    return -1;
  }

  /* Laplace smoothing: add 1 to every cell */
  for (unsigned int i = 0; i < charset_len; i++)
    model->pos0_freq[i]++;

  for (size_t i = 0; i < (size_t)charset_len * charset_len; i++)
    model->bigram_freq[i]++;

  markov_build_sorted(model);
  return 0;
}

/* -------------------------------------------------------------------------
 * markov_save
 * ------------------------------------------------------------------------- */

int markov_save(const char *path, const markov_model *model)
{
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    fprintf(stderr, "markov_save: cannot open '%s' for writing\n", path);
    return -1;
  }

  /* Magic */
  if (fwrite(MARKOV_MAGIC, 1, 4, fp) != 4)
    goto write_error;

  /* Version */
  uint32_t ver = MARKOV_VERSION;
  if (fwrite(&ver, sizeof(uint32_t), 1, fp) != 1)
    goto write_error;

  /* Charset length */
  uint32_t clen = model->charset_len;
  if (fwrite(&clen, sizeof(uint32_t), 1, fp) != 1)
    goto write_error;

  /* Charset bytes */
  if (fwrite(model->charset, 1, model->charset_len, fp) != model->charset_len)
    goto write_error;

  /* pos0_freq array */
  if (fwrite(model->pos0_freq, sizeof(uint32_t), model->charset_len, fp)
      != model->charset_len)
    goto write_error;

  /* bigram_freq array */
  size_t bigram_count = (size_t)model->charset_len * model->charset_len;
  if (fwrite(model->bigram_freq, sizeof(uint32_t), bigram_count, fp)
      != bigram_count)
    goto write_error;

  fclose(fp);
  return 0;

write_error:
  fprintf(stderr, "markov_save: write error on '%s'\n", path);
  fclose(fp);
  remove(path);
  return -1;
}

/* -------------------------------------------------------------------------
 * markov_load
 * ------------------------------------------------------------------------- */

int markov_load(const char *path, markov_model *model)
{
  memset(model, 0, sizeof(*model));

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "markov_load: cannot open '%s'\n", path);
    return -1;
  }

  /* Magic */
  char magic[4];
  if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, MARKOV_MAGIC, 4) != 0) {
    fprintf(stderr, "markov_load: bad magic in '%s'\n", path);
    fclose(fp);
    return -1;
  }

  /* Version - split into two checks so ver is initialised before use */
  uint32_t ver;
  if (fread(&ver, sizeof(uint32_t), 1, fp) != 1) {
    fprintf(stderr, "markov_load: truncated version field in '%s'\n", path);
    fclose(fp);
    return -1;
  }
  if (ver != MARKOV_VERSION) {
    fprintf(stderr, "markov_load: unsupported version %u in '%s'\n", ver, path);
    fclose(fp);
    return -1;
  }

  /* Charset length */
  uint32_t clen;
  if (fread(&clen, sizeof(uint32_t), 1, fp) != 1 || clen == 0 || clen > 256) {
    fprintf(stderr, "markov_load: invalid charset_len in '%s'\n", path);
    fclose(fp);
    return -1;
  }
  model->charset_len = clen;

  /* Charset bytes */
  if (fread(model->charset, 1, clen, fp) != clen) {
    fprintf(stderr, "markov_load: truncated charset in '%s'\n", path);
    fclose(fp);
    return -1;
  }

  /* Allocate arrays */
  model->pos0_freq     = calloc(clen, sizeof(uint32_t));
  model->bigram_freq   = calloc((size_t)clen * clen, sizeof(uint32_t));
  model->sorted_pos0   = calloc(clen, sizeof(uint8_t));
  model->sorted_bigram = calloc((size_t)clen * clen, sizeof(uint8_t));

  if (!model->pos0_freq || !model->bigram_freq ||
      !model->sorted_pos0 || !model->sorted_bigram) {
    fprintf(stderr, "markov_load: out of memory\n");
    fclose(fp);
    markov_free(model);
    return -1;
  }

  /* pos0_freq */
  if (fread(model->pos0_freq, sizeof(uint32_t), clen, fp) != clen) {
    fprintf(stderr, "markov_load: truncated pos0_freq in '%s'\n", path);
    fclose(fp);
    markov_free(model);
    return -1;
  }

  /* bigram_freq */
  size_t bigram_count = (size_t)clen * clen;
  if (fread(model->bigram_freq, sizeof(uint32_t), bigram_count, fp)
      != bigram_count) {
    fprintf(stderr, "markov_load: truncated bigram_freq in '%s'\n", path);
    fclose(fp);
    markov_free(model);
    return -1;
  }

  fclose(fp);

  markov_build_sorted(model);
  return 0;
}

/* -------------------------------------------------------------------------
 * index_to_plaintext_markov_cpu
 * ------------------------------------------------------------------------- */

void index_to_plaintext_markov_cpu(uint64_t index, const markov_model *model,
                                   unsigned int plaintext_len,
                                   unsigned char *plaintext)
{
  unsigned int n = model->charset_len;

  /* Position 0: decode from sorted_pos0 */
  plaintext[0] = (unsigned char)model->charset[model->sorted_pos0[index % n]];
  index /= n;

  /* Positions 1..plaintext_len-1: decode from bigram row of previous char */
  for (unsigned int i = 1; i < plaintext_len; i++) {
    /* Find index of previous character in charset */
    unsigned int prev_idx = 0;
    unsigned char prev_ch = plaintext[i - 1];
    for (unsigned int j = 0; j < n; j++) {
      if ((unsigned char)model->charset[j] == prev_ch) {
        prev_idx = j;
        break;
      }
    }
    assert(prev_idx < n); /* charset chars are always in the model; caller bug otherwise */
    const uint8_t *row = model->sorted_bigram + (size_t)prev_idx * n;
    plaintext[i] = (unsigned char)model->charset[row[index % n]];
    index /= n;
  }
}

/* -------------------------------------------------------------------------
 * markov_free
 * ------------------------------------------------------------------------- */

void markov_free(markov_model *model)
{
  free(model->pos0_freq);
  free(model->bigram_freq);
  free(model->sorted_pos0);
  free(model->sorted_bigram);
  memset(model, 0, sizeof(*model));
}
