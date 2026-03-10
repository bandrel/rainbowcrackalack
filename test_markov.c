/*
 * Rainbow Crackalack: test_markov.c
 * CPU-only unit tests for markov.h / markov.c.
 */

#include <inttypes.h>
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

    uint64_t pos0_freq[3]    = {10, 30, 20};
    uint64_t bigram_freq[9]  = {
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

    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {
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
        fprintf(stderr, "MT-01 failed: a->b freq=%"PRIu64", a->c freq=%"PRIu64"\n",
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
                       m.charset_len * sizeof(uint64_t)) != 0) {
                fprintf(stderr, "MT-03 failed: pos0_freq mismatch\n");
                ok = 0;
            }
            if (memcmp(m2.bigram_freq, m.bigram_freq,
                       (size_t)m.charset_len * m.charset_len * sizeof(uint64_t)) != 0) {
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
 * Group E: markov_train edge cases
 * Tests ME-04 through ME-07
 * ------------------------------------------------------------------------- */

static int group_e(void)
{
    int ok = 1;
    markov_model m;

    /* ME-04: charset_len == 0 returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_train("/dev/null", "abc", 0, &m) != -1) {
        fprintf(stderr, "ME-04 failed: expected -1 for charset_len==0\n");
        ok = 0;
    }

    /* ME-05: charset_len > 256 returns -1 */
    memset(&m, 0, sizeof(m));
    if (markov_train("/dev/null", "abc", 257, &m) != -1) {
        fprintf(stderr, "ME-05 failed: expected -1 for charset_len==257\n");
        ok = 0;
    }

    /* ME-06: all words outside charset returns -1 */
    {
        char path[128];
        snprintf(path, sizeof(path),
                 "/tmp/test_markov_oob_%d.txt", (int)getpid());
        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "ME-06: cannot create file\n");
            ok = 0;
        } else {
            fprintf(fp, "XYZ\n999\n");
            fclose(fp);

            memset(&m, 0, sizeof(m));
            if (markov_train(path, "abc", 3, &m) != -1) {
                fprintf(stderr, "ME-06 failed: expected -1 for all-OOB corpus\n");
                ok = 0;
            }
            remove(path);
        }
    }

    /* ME-07: single-character words produce valid model */
    {
        char path[128];
        snprintf(path, sizeof(path),
                 "/tmp/test_markov_single_%d.txt", (int)getpid());
        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "ME-07: cannot create file\n");
            ok = 0;
        } else {
            for (int i = 0; i < 10; i++) fprintf(fp, "a\n");
            for (int i = 0; i < 5; i++) fprintf(fp, "b\n");
            fclose(fp);

            memset(&m, 0, sizeof(m));
            if (markov_train(path, "abc", 3, &m) != 0) {
                fprintf(stderr, "ME-07 failed: markov_train returned error\n");
                ok = 0;
            } else {
                /* 'a' should be most frequent at pos0 (10+1=11 vs 5+1=6 vs 0+1=1) */
                if (m.sorted_pos0[0] != 0) {
                    fprintf(stderr, "ME-07 failed: sorted_pos0[0]=%u, expected 0 ('a')\n",
                            (unsigned)m.sorted_pos0[0]);
                    ok = 0;
                }
                /* All bigram counts should be exactly 1 (Laplace only, no observed bigrams) */
                int bigrams_ok = 1;
                for (unsigned int i = 0; i < 9; i++) {
                    if (m.bigram_freq[i] != 1) {
                        bigrams_ok = 0;
                        break;
                    }
                }
                if (!bigrams_ok) {
                    fprintf(stderr, "ME-07 failed: expected uniform bigram freq (all 1)\n");
                    ok = 0;
                }
                markov_free(&m);
            }
            remove(path);
        }
    }

    return ok;
}


/* -------------------------------------------------------------------------
 * Group F: index_to_plaintext_markov_cpu boundary tests
 * Tests IMP-03 through IMP-06
 * ------------------------------------------------------------------------- */

static int group_f(void)
{
    int ok = 1;

    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {
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
        fprintf(stderr, "group_f: out of memory\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }

    markov_build_sorted(&m);

    unsigned char pt[4];

    /* IMP-03: last valid 2-char index (n*n - 1 = 8) wraps correctly */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(8, &m, 2, pt);
    /* index=8: pos0=sorted_pos0[8%3]=sorted_pos0[2]; index=8/3=2
     *          pos1=sorted_bigram[prev*3 + 2%3] */
    if (pt[0] == 0 || pt[1] == 0) {
        fprintf(stderr, "IMP-03 failed: got null chars for max 2-char index\n");
        ok = 0;
    }

    /* IMP-04: index 0 with length 1 -> single most probable char */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 1, pt);
    if (pt[0] != 'b') {
        fprintf(stderr, "IMP-04 failed: got '%c', expected 'b'\n", (char)pt[0]);
        ok = 0;
    }

    /* IMP-05: 3-char plaintext at index 0 -> most probable trigram */
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(0, &m, 3, pt);
    /* pos0: sorted_pos0[0]=1 -> 'b'
     * pos1: prev='b'(1), sorted_bigram[1*3+0]=2 -> 'c'
     * pos2: prev='c'(2), sorted_bigram[2*3+0]=0 -> 'a' */
    if (pt[0] != 'b' || pt[1] != 'c' || pt[2] != 'a') {
        fprintf(stderr, "IMP-05 failed: got \"%c%c%c\", expected \"bca\"\n",
                (char)pt[0], (char)pt[1], (char)pt[2]);
        ok = 0;
    }

    /* IMP-06: all indices [0, n^2) produce unique 2-char plaintexts */
    unsigned int seen[9];
    memset(seen, 0, sizeof(seen));
    int unique = 1;
    for (uint64_t idx = 0; idx < 9; idx++) {
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(idx, &m, 2, pt);
        /* Hash the 2-char result to check uniqueness */
        unsigned int key = 0;
        for (unsigned int ci = 0; ci < 3; ci++) {
            if ((unsigned char)m.charset[ci] == pt[0]) {
                for (unsigned int cj = 0; cj < 3; cj++) {
                    if ((unsigned char)m.charset[cj] == pt[1]) {
                        key = ci * 3 + cj;
                    }
                }
            }
        }
        if (seen[key]) {
            fprintf(stderr, "IMP-06 failed: duplicate at index %llu\n",
                    (unsigned long long)idx);
            unique = 0;
            break;
        }
        seen[key] = 1;
    }
    if (!unique) ok = 0;

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return ok;
}


/* -------------------------------------------------------------------------
 * Group G: Markov model quality / viability tests
 * Tests MQ-01 through MQ-04
 * ------------------------------------------------------------------------- */

static int group_g(void)
{
    int ok = 1;

    /* MQ-01: Common-first ordering.
     * Corpus (charset "abc"): "ba"x20, "ca"x10, "bc"x5, all others x1.
     * "ba" is the most frequent bigram, so index 0 must return "ba". */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq01_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-01: cannot create corpus\n"); ok = 0; goto mq01_done; }
        for (int i = 0; i < 20; i++) fprintf(fp, "ba\n");
        for (int i = 0; i < 10; i++) fprintf(fp, "ca\n");
        for (int i = 0; i <  5; i++) fprintf(fp, "bc\n");
        /* low-frequency fillers */
        fprintf(fp, "ab\nac\nbb\ncb\ncc\naa\n");
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, &m) != 0) {
            fprintf(stderr, "MQ-01: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq01_done;
        }

        unsigned char pt[3];
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(0, &m, 2, pt);
        if (pt[0] != 'b' || pt[1] != 'a') {
            fprintf(stderr, "MQ-01 failed: index 0 returned \"%c%c\", expected \"ba\"\n",
                    (char)pt[0], (char)pt[1]);
            ok = 0;
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq01_done:;

    /* MQ-02: Monotone probability ordering.
     * Corpus (charset "ab"): "aa"x100, "bb"x1.
     * After Laplace smoothing "aa" has far higher probability, so index 0
     * must return "aa" and the index of "bb" must be > 0. */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq02_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-02: cannot create corpus\n"); ok = 0; goto mq02_done; }
        for (int i = 0; i < 100; i++) fprintf(fp, "aa\n");
        fprintf(fp, "bb\n");
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "ab", 2, &m) != 0) {
            fprintf(stderr, "MQ-02: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq02_done;
        }

        unsigned char pt[3];
        memset(pt, 0, sizeof(pt));
        index_to_plaintext_markov_cpu(0, &m, 2, pt);
        if (pt[0] != 'a' || pt[1] != 'a') {
            fprintf(stderr, "MQ-02 failed: index 0 returned \"%c%c\", expected \"aa\"\n",
                    (char)pt[0], (char)pt[1]);
            ok = 0;
        }

        /* Find which index gives "bb" and verify it is > 0 */
        int found_bb_at = -1;
        for (uint64_t idx = 0; idx < 4; idx++) {
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu(idx, &m, 2, pt);
            if (pt[0] == 'b' && pt[1] == 'b') {
                found_bb_at = (int)idx;
                break;
            }
        }
        if (found_bb_at <= 0) {
            fprintf(stderr, "MQ-02 failed: \"bb\" found at index %d (expected > 0)\n",
                    found_bb_at);
            ok = 0;
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq02_done:;

    /* MQ-03: Top-N coverage.
     * Corpus (charset "abc"): "ba"x50, "bc"x30, "ca"x20, all others x2.
     * The three most-probable bigrams are "ba", "bc", "ca".
     * Verify all three appear within the first 6 Markov indices (2x slack). */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq03_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-03: cannot create corpus\n"); ok = 0; goto mq03_done; }
        for (int i = 0; i < 50; i++) fprintf(fp, "ba\n");
        for (int i = 0; i < 30; i++) fprintf(fp, "bc\n");
        for (int i = 0; i < 20; i++) fprintf(fp, "ca\n");
        for (int i = 0; i <  2; i++) {
            fprintf(fp, "aa\nab\nac\nbb\ncb\ncc\n");
        }
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, &m) != 0) {
            fprintf(stderr, "MQ-03: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq03_done;
        }

        /* Collect first 6 Markov-ordered plaintexts */
        char first6[6][3];
        for (int idx = 0; idx < 6; idx++) {
            unsigned char pt[3];
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu((uint64_t)idx, &m, 2, pt);
            first6[idx][0] = (char)pt[0];
            first6[idx][1] = (char)pt[1];
            first6[idx][2] = '\0';
        }

        const char *top3[] = {"ba", "bc", "ca"};
        for (int t = 0; t < 3; t++) {
            int found = 0;
            for (int idx = 0; idx < 6; idx++) {
                if (first6[idx][0] == top3[t][0] && first6[idx][1] == top3[t][1]) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "MQ-03 failed: \"%s\" not in first 6 Markov indices\n",
                        top3[t]);
                ok = 0;
            }
        }

        markov_free(&m);
        remove(corpus_path);
    }
mq03_done:;

    /* MQ-04: Uniform corpus does not crash and produces unique plaintexts.
     * Corpus (charset "abc"): each of the 9 bigrams appears exactly twice.
     * After Laplace smoothing all bigram freqs are equal.
     * Verify: no crashes and all 9 indices [0,9) produce unique 2-char plaintexts. */
    {
        char corpus_path[128];
        snprintf(corpus_path, sizeof(corpus_path),
                 "/tmp/test_markov_mq04_%d.txt", (int)getpid());
        FILE *fp = fopen(corpus_path, "w");
        if (!fp) { fprintf(stderr, "MQ-04: cannot create corpus\n"); ok = 0; goto mq04_done; }
        const char *bigrams[] = {"aa","ab","ac","ba","bb","bc","ca","cb","cc"};
        for (int i = 0; i < 9; i++) {
            fprintf(fp, "%s\n", bigrams[i]);
            fprintf(fp, "%s\n", bigrams[i]);
        }
        fclose(fp);

        markov_model m;
        memset(&m, 0, sizeof(m));
        if (markov_train(corpus_path, "abc", 3, &m) != 0) {
            fprintf(stderr, "MQ-04: markov_train failed\n");
            ok = 0; remove(corpus_path); goto mq04_done;
        }

        /* All 9 indices must produce unique 2-char plaintexts */
        unsigned int seen[9];
        memset(seen, 0, sizeof(seen));
        int unique = 1;
        for (uint64_t idx = 0; idx < 9; idx++) {
            unsigned char pt[3];
            memset(pt, 0, sizeof(pt));
            index_to_plaintext_markov_cpu(idx, &m, 2, pt);
            int ci = -1, cj = -1;
            for (unsigned int k = 0; k < 3; k++) {
                if ((unsigned char)m.charset[k] == pt[0]) ci = (int)k;
                if ((unsigned char)m.charset[k] == pt[1]) cj = (int)k;
            }
            if (ci < 0 || cj < 0) {
                fprintf(stderr, "MQ-04 failed: char not in charset at idx %llu\n",
                        (unsigned long long)idx);
                unique = 0;
                break;
            }
            int key = ci * 3 + cj;
            if (seen[key]) {
                fprintf(stderr, "MQ-04 failed: duplicate plaintext at idx %llu\n",
                        (unsigned long long)idx);
                unique = 0;
                break;
            }
            seen[key] = 1;
        }
        if (!unique) ok = 0;

        markov_free(&m);
        remove(corpus_path);
    }
mq04_done:;

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
    ok &= group_e();
    ok &= group_f();
    ok &= group_g();

    return ok;
}
