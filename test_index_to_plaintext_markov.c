/*
 * Rainbow Crackalack: test_index_to_plaintext_markov.c
 * GPU unit tests for index_to_plaintext_markov kernel.
 *
 * Builds a small synthetic Markov model in-memory, then verifies that the GPU
 * kernel produces the same plaintexts as the CPU reference for every test
 * vector.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_backend.h"

#include "markov.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_index_to_plaintext_markov.h"

/*
 * Synthetic model used across all tests:
 *   charset = "abc" (len=3)
 *   pos0_freq  = {10, 30, 20}
 *   bigram_freq row a(0): {5, 15, 10}
 *               row b(1): {1,  1, 50}
 *               row c(2): {8,  2,  4}
 *
 * After markov_build_sorted:
 *   sorted_pos0          = {1, 2, 0}        (b, c, a)
 *   sorted_bigram row 0  = {1, 2, 0}        (b, c, a)
 *   sorted_bigram row 1  = {2, 0, 1}        (c, a, b)   tie broken by index
 *   sorted_bigram row 2  = {0, 2, 1}        (a, c, b)
 *
 * Flat sorted_bigram[9] = {1,2,0, 2,0,1, 0,2,1}
 */
static const char TEST_CHARSET[]     = "abc";
static const unsigned int CHARSET_LEN = 3;

struct i2p_markov_test {
    uint64_t     index;
    unsigned int plaintext_len;
    char         expected[MAX_PLAINTEXT_LEN + 1];
};

/* All 9 unique 2-char plaintexts + 1-char and 3-char edge cases.
 * Values derived by hand-tracing the algorithm in markov.c / rt_markov.cl. */
static struct i2p_markov_test markov_i2p_tests[] = {
    /* 2-char: all 9 permutations */
    {0, 2, "bc"},
    {1, 2, "ca"},
    {2, 2, "ab"},
    {3, 2, "ba"},
    {4, 2, "cc"},
    {5, 2, "ac"},
    {6, 2, "bb"},
    {7, 2, "cb"},
    {8, 2, "aa"},
    /* 1-char: most probable first char */
    {0, 1, "b"},
    {1, 1, "c"},
    {2, 1, "a"},
    /* 3-char: index 0 traces b->c->a */
    {0, 3, "bca"},
};


static int cpu_test_i2p_markov(const markov_model *model,
                                uint64_t index,
                                unsigned int plaintext_len,
                                const char *expected)
{
    unsigned char pt[MAX_PLAINTEXT_LEN + 1];
    memset(pt, 0, sizeof(pt));
    index_to_plaintext_markov_cpu(index, model, plaintext_len, pt);

    if (memcmp(pt, expected, plaintext_len) == 0)
        return 1;

    fprintf(stderr,
            "CPU Markov i2p error: index=%"PRIu64" len=%u expected=[%s] got=[%.*s]\n",
            index, plaintext_len, expected, (int)plaintext_len, (char *)pt);
    return 0;
}


static int gpu_test_i2p_markov(gpu_device device, gpu_context context,
                                gpu_kernel kernel,
                                const uint8_t *sorted_pos0,
                                const uint8_t *sorted_bigram,
                                uint64_t index,
                                unsigned int plaintext_len,
                                const char *expected)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer charset_buf  = NULL, charset_len_buf = NULL;
    gpu_buffer plen_buf     = NULL, index_buf       = NULL;
    gpu_buffer plaintext_buf = NULL, plen_out_buf   = NULL;
    gpu_buffer debug_buf    = NULL;
    gpu_buffer sorted_pos0_buf = NULL, sorted_bigram_buf = NULL;

    unsigned char *plaintext = NULL;
    unsigned char *debug_ptr = NULL;

    gpu_uint charset_len_val = CHARSET_LEN;
    gpu_uint plen_val        = (gpu_uint)plaintext_len;
    gpu_ulong index_val      = (gpu_ulong)index;
    gpu_uint plen_out        = 0;

    queue = CLCREATEQUEUE(context, device);

    plaintext = calloc(MAX_PLAINTEXT_LEN, sizeof(unsigned char));
    if (!plaintext) { fprintf(stderr, "OOM in gpu_test_i2p_markov\n"); exit(-1); }

    CLCREATEARG_ARRAY(0, charset_buf,     CL_RO, TEST_CHARSET,  CHARSET_LEN);
    CLCREATEARG(1, charset_len_buf,       CL_RO, charset_len_val, sizeof(gpu_uint));
    CLCREATEARG(2, plen_buf,              CL_RO, plen_val,        sizeof(gpu_uint));
    CLCREATEARG(3, index_buf,             CL_RO, index_val,       sizeof(gpu_ulong));
    CLCREATEARG_ARRAY(4, plaintext_buf,   CL_WO, plaintext, MAX_PLAINTEXT_LEN);
    CLCREATEARG(5, plen_out_buf,          CL_WO, plen_out,        sizeof(gpu_uint));
    CLCREATEARG_DEBUG(6, debug_buf, debug_ptr);
    CLCREATEARG_ARRAY(7, sorted_pos0_buf, CL_RO, sorted_pos0, CHARSET_LEN * sizeof(uint8_t));
    CLCREATEARG_ARRAY(8, sorted_bigram_buf, CL_RO, sorted_bigram,
                      CHARSET_LEN * CHARSET_LEN * sizeof(uint8_t));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(plaintext_buf, MAX_PLAINTEXT_LEN, plaintext);
    CLREADBUFFER(plen_out_buf,  sizeof(gpu_uint),  &plen_out);

    if (plen_out == plaintext_len && memcmp(plaintext, expected, plaintext_len) == 0) {
        test_passed = 1;
    } else {
        fprintf(stderr,
                "GPU Markov i2p error: index=%"PRIu64" len=%u "
                "expected=[%s] got=[%.*s] (plen_out=%u)\n",
                index, plaintext_len, expected,
                (int)plen_out, (char *)plaintext, plen_out);
    }

    CLFREEBUFFER(charset_buf);
    CLFREEBUFFER(charset_len_buf);
    CLFREEBUFFER(plen_buf);
    CLFREEBUFFER(index_buf);
    CLFREEBUFFER(plaintext_buf);
    CLFREEBUFFER(plen_out_buf);
    CLFREEBUFFER(debug_buf);
    CLFREEBUFFER(sorted_pos0_buf);
    CLFREEBUFFER(sorted_bigram_buf);
    CLRELEASEQUEUE(queue);

    FREE(plaintext);
    FREE(debug_ptr);
    return test_passed;
}


int test_index_to_plaintext_markov(gpu_device device, gpu_context context,
                                    gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(markov_i2p_tests) /
                                            sizeof(markov_i2p_tests[0]));

    /* Build synthetic model */
    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {5, 15, 10,
                                1,  1, 50,
                                8,  2,  4};

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = CHARSET_LEN;
    memcpy(m.charset, TEST_CHARSET, CHARSET_LEN);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(CHARSET_LEN * sizeof(uint8_t));
    m.sorted_bigram = malloc(CHARSET_LEN * CHARSET_LEN * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "test_index_to_plaintext_markov: OOM\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }
    markov_build_sorted(&m);

    for (i = 0; i < num_tests; i++) {
        const struct i2p_markov_test *t = &markov_i2p_tests[i];

        tests_passed &= cpu_test_i2p_markov(&m, t->index, t->plaintext_len, t->expected);
        tests_passed &= gpu_test_i2p_markov(device, context, kernel,
                                             m.sorted_pos0, m.sorted_bigram,
                                             t->index, t->plaintext_len,
                                             t->expected);
    }

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return tests_passed;
}
