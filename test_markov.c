/*
 * Rainbow Crackalack: test_markov.c
 * CPU-only unit tests for markov.h / markov.c.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "markov.h"
#include "test_markov.h"


/* -------------------------------------------------------------------------
 * Group A: markov_build_sorted
 * Tests MS-01 through MS-04
 * ------------------------------------------------------------------------- */

static int group_a(void)
{
    int ok = 1;

    uint32_t pos0_freq[3]    = {10, 30, 20};
    uint32_t bigram_freq[9]  = {
        5, 15, 10,   /* prev='a'(0): b most likely */
        1,  1, 50,   /* prev='b'(1): c most likely */
        8,  2,  4    /* prev='c'(2): a most likely */
    };

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len  = 3;
    memcpy(m.charset, "abc", 3);
    m.pos0_freq    = pos0_freq;
    m.bigram_freq  = bigram_freq;
    m.sorted_pos0  = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_a: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    /* MS-01: rank-0 for position 0 is 'b' (charset index 1, freq=30) */
    if (m.sorted_pos0[0] != 1) {
        fprintf(stderr, "MS-01 failed: sorted_pos0[0]=%u, expected 1\n",
                (unsigned)m.sorted_pos0[0]);
        ok = 0;
    }

    /* MS-02: rank-1 for position 0 is 'c' (charset index 2, freq=20) */
    if (m.sorted_pos0[1] != 2) {
        fprintf(stderr, "MS-02 failed: sorted_pos0[1]=%u, expected 2\n",
                (unsigned)m.sorted_pos0[1]);
        ok = 0;
    }

    /* MS-03: prev='a'(row 0), rank-0 next is 'b' (index 1, freq=15) */
    if (m.sorted_bigram[0 * 3 + 0] != 1) {
        fprintf(stderr, "MS-03 failed: sorted_bigram[0]=%u, expected 1\n",
                (unsigned)m.sorted_bigram[0 * 3 + 0]);
        ok = 0;
    }

    /* MS-04: prev='b'(row 1), rank-0 next is 'c' (index 2, freq=50) */
    if (m.sorted_bigram[1 * 3 + 0] != 2) {
        fprintf(stderr, "MS-04 failed: sorted_bigram[3]=%u, expected 2\n",
                (unsigned)m.sorted_bigram[1 * 3 + 0]);
        ok = 0;
    }

    /* Nullify stack-backed pointers before markov_free so it doesn't free them. */
    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group B: index_to_plaintext_markov_cpu
 * Tests IMP-01 through IMP-02
 * ------------------------------------------------------------------------- */

static int group_b(void)
{
    int ok = 1;

    uint32_t pos0_freq[3]   = {10, 30, 20};
    uint32_t bigram_freq[9] = {
        5, 15, 10,
        1,  1, 50,
        8,  2,  4
    };

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = 3;
    memcpy(m.charset, "abc", 3);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(3 * sizeof(uint8_t));
    m.sorted_bigram = malloc(9 * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "group_b: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    unsigned char pt[3];

    /* IMP-01: index 0 -> "bc"
     * pos0: sorted_pos0[0]=1 -> 'b'; index=0/3=0
     * pos1: prev='b'(idx 1), sorted_bigram[1*3+0]=2 -> 'c' */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 2, pt);
    if (pt[0] != 'b' || pt[1] != 'c') {
        fprintf(stderr, "IMP-01 failed: got \"%c%c\", expected \"bc\"\n",
                (char)pt[0], (char)pt[1]);
        ok = 0;
    }

    /* IMP-02: index 1 -> "ca"
     * pos0: sorted_pos0[1]=2 -> 'c'; index=1/3=0
     * pos1: prev='c'(idx 2), sorted_bigram[2*3+0]=0 -> 'a' */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(1, &m, 2, pt);
    if (pt[0] != 'c' || pt[1] != 'a') {
        fprintf(stderr, "IMP-02 failed: got \"%c%c\", expected \"ca\"\n",
                (char)pt[0], (char)pt[1]);
        ok = 0;
    }

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group C: markov_train round-trip (train -> save -> load -> compare)
 * Tests MT-01 through MT-03
 * ------------------------------------------------------------------------- */

static int group_c(void)
{
    int ok = 1;
    char corpus_path[128];
    char model_path[128];
    snprintf(corpus_path, sizeof(corpus_path),
             "/tmp/test_markov_corpus_%d.txt", (int)getpid());
    snprintf(model_path, sizeof(model_path),
             "/tmp/test_markov_rt_%d.markov", (int)getpid());

    /* Write synthetic corpus: "ab" x5, "ac" x1, "bc" x3 */
    FILE *fp = fopen(corpus_path, "w");
    if (!fp) {
        fprintf(stderr, "MT: cannot create corpus '%s'\n", corpus_path);
        return 0;
    }
    for (int i = 0; i < 5; i++) fprintf(fp, "ab\n");
    fprintf(fp, "ac\n");
    for (int i = 0; i < 3; i++) fprintf(fp, "bc\n");
    fclose(fp);

    markov_model m;
    memset(&m, 0, sizeof(m));
    if (markov_train(corpus_path, "abc", 3, &m) != 0) {
        fprintf(stderr, "MT: markov_train failed\n");
        remove(corpus_path);
        /* markov_train zeroes the model on failure, so markov_free is safe but a no-op. */
        markov_free(&m);
        return 0;
    }

    /* MT-01: Laplace-smoothed a->b count (5+1=6) > a->c count (1+1=2) */
    if (m.bigram_freq[0 * 3 + 1] <= m.bigram_freq[0 * 3 + 2]) {
        fprintf(stderr, "MT-01 failed: a->b freq=%u, a->c freq=%u\n",
                m.bigram_freq[0 * 3 + 1], m.bigram_freq[0 * 3 + 2]);
        ok = 0;
    }

    /* MT-02: sorted_bigram[0*3+0] == 1 (prev='a', rank-0 next is 'b') */
    if (m.sorted_bigram[0 * 3 + 0] != 1) {
        fprintf(stderr, "MT-02 failed: sorted_bigram[0]=%u, expected 1\n",
                (unsigned)m.sorted_bigram[0 * 3 + 0]);
        ok = 0;
    }

    /* MT-03: save, reload, compare */
    if (markov_save(model_path, &m) != 0) {
        fprintf(stderr, "MT-03: markov_save failed\n");
        ok = 0;
    } else {
        markov_model m2;
        memset(&m2, 0, sizeof(m2));
        if (markov_load(model_path, &m2) != 0) {
            fprintf(stderr, "MT-03: markov_load failed\n");
            ok = 0;
        } else {
            if (m2.charset_len != m.charset_len) {
                fprintf(stderr, "MT-03 failed: charset_len %u != %u\n",
                        m2.charset_len, m.charset_len);
                ok = 0;
            }
            if (memcmp(m2.pos0_freq, m.pos0_freq,
                       m.charset_len * sizeof(uint32_t)) != 0) {
                fprintf(stderr, "MT-03 failed: pos0_freq mismatch\n");
                ok = 0;
            }
            if (memcmp(m2.bigram_freq, m.bigram_freq,
                       (size_t)m.charset_len * m.charset_len * sizeof(uint32_t)) != 0) {
                fprintf(stderr, "MT-03 failed: bigram_freq mismatch\n");
                ok = 0;
            }
            markov_free(&m2);
        }
        remove(model_path);
    }

    markov_free(&m);
    remove(corpus_path);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group D: error handling
 * Tests ME-01 through ME-03
 * ------------------------------------------------------------------------- */

static int group_d(void)
{
    int ok = 1;
    markov_model m;

    /* ME-01: load from non-existent file returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_load("/tmp/no_such_file_rca_xyz.markov", &m) != -1) {
        fprintf(stderr, "ME-01 failed: expected -1 for missing file\n");
        ok = 0;
    }

    /* ME-02: load file with wrong magic returns -1
     * Write 12 bytes: "XXXX" + version(1) as uint32 + charset_len(3) as uint32 */
    char bad_path[128];
    snprintf(bad_path, sizeof(bad_path),
             "/tmp/test_markov_bad_%d.markov", (int)getpid());
    FILE *fp = fopen(bad_path, "wb");
    if (!fp) {
        fprintf(stderr, "ME-02: cannot create bad markov file\n");
        ok = 0;
    } else {
        fwrite("XXXX", 1, 4, fp);
        uint32_t ver = 1;
        uint32_t clen = 3;
        fwrite(&ver, sizeof(uint32_t), 1, fp);
        fwrite(&clen, sizeof(uint32_t), 1, fp);
        fclose(fp);

        memset(&m, 0, sizeof(m));
        if (markov_load(bad_path, &m) != -1) {
            fprintf(stderr, "ME-02 failed: expected -1 for bad magic\n");
            ok = 0;
        }
        remove(bad_path);
    }

    /* ME-03: train on empty file returns -1 */
    char empty_path[128];
    snprintf(empty_path, sizeof(empty_path),
             "/tmp/test_markov_empty_%d.txt", (int)getpid());
    fp = fopen(empty_path, "w");
    if (!fp) {
        fprintf(stderr, "ME-03: cannot create empty file\n");
        ok = 0;
    } else {
        fclose(fp);
        memset(&m, 0, sizeof(m));
        if (markov_train(empty_path, "abc", 3, &m) != -1) {
            fprintf(stderr, "ME-03 failed: expected -1 for empty corpus\n");
            ok = 0;
        }
        remove(empty_path);
    }

    return ok;
}


/* -------------------------------------------------------------------------
 * test_markov - entry point
 * ------------------------------------------------------------------------- */

int test_markov(void)
{
    int ok = 1;

    ok &= group_a();
    ok &= group_b();
    ok &= group_c();
    ok &= group_d();

    return ok;
}
