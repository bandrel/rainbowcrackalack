/*
 * Rainbow Crackalack: test_chain_markov.c
 * GPU chain tests for the Markov attack using the crackalack_markov kernel.
 *
 * Strategy: compute the expected chain endpoint with an inline CPU reference
 * (index_to_plaintext_markov_cpu -> ntlm_hash -> hash_to_index), then verify
 * the GPU crackalack_markov kernel produces the same endpoint.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "markov.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_chain_markov.h"

/*
 * Same synthetic model as test_index_to_plaintext_markov.c:
 *   charset = "abc" (len=3), fixed plaintext_len=2
 *   pspace_total = 3^2 = 9
 */
static const char MC_CHARSET[]     = "abc";
static const unsigned int MC_CHARSET_LEN  = 3;
static const unsigned int MC_PLAINTEXT_LEN = 2;

struct markov_chain_test {
    uint64_t     start;
    unsigned int chain_len;
    unsigned int table_index;
};

static struct markov_chain_test markov_chain_tests[] = {
    {0UL,  100,  0},
    {1UL,  100,  0},
    {5UL,  200,  1},
    {8UL,  100,  0},
    /* chain_len=1: loop runs 0 iterations, end must equal start */
    {3UL,    1,  0},
};


/*
 * CPU reference Markov chain generator.
 * Walks chain_len-1 steps: i2p_markov -> ntlm_hash -> hash_to_index.
 */
static uint64_t cpu_markov_chain(uint64_t start, unsigned int chain_len,
                                  unsigned int table_index,
                                  const markov_model *model,
                                  unsigned int plaintext_len)
{
    uint64_t index = start;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);
    unsigned int pos;

    /* pspace_total = charset_len ^ plaintext_len */
    uint64_t pspace_total = 1;
    for (unsigned int i = 0; i < plaintext_len; i++)
        pspace_total *= model->charset_len;

    for (pos = 0; pos < chain_len - 1; pos++) {
        unsigned char plaintext[MAX_PLAINTEXT_LEN];
        unsigned char hash[16];

        memset(plaintext, 0, sizeof(plaintext));
        index_to_plaintext_markov_cpu(index, model, plaintext_len, plaintext);
        ntlm_hash((char *)plaintext, plaintext_len, hash);
        index = hash_to_index(hash, 16, reduction_offset, pspace_total, pos);
    }
    return index;
}


/*
 * GPU test: call crackalack_markov kernel and compare endpoint with expected.
 *
 * crackalack_markov arg layout:
 *   0  hash_type
 *   1  charset
 *   2  charset_len
 *   3  plaintext_len_min (unused in markov but must be present)
 *   4  plaintext_len_max
 *   5  reduction_offset
 *   6  chain_len
 *   7  g_indices (RW)
 *   8  pos_start
 *   9  plaintext_space_up_to_index (unused in markov)
 *  10  plaintext_space_total
 *  11  sorted_pos0 (constant)
 *  12  sorted_bigram (constant)
 *  13  max_positions
 */
static int gpu_test_markov_chain(gpu_device device, gpu_context context,
                                  gpu_kernel kernel,
                                  const struct markov_chain_test *t,
                                  uint64_t expected_end,
                                  const markov_model *model,
                                  unsigned int plaintext_len)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_uint hash_type         = HASH_NTLM;
    gpu_uint charset_len_val   = MC_CHARSET_LEN;
    gpu_uint plen_min          = (gpu_uint)plaintext_len;
    gpu_uint plen_max          = (gpu_uint)plaintext_len;
    gpu_uint reduction_offset  = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);
    gpu_uint chain_len_val     = (gpu_uint)t->chain_len;
    gpu_uint pos_start         = 0;

    /* pspace_total = charset_len ^ plaintext_len */
    gpu_ulong pspace_total = 1;
    for (unsigned int i = 0; i < plaintext_len; i++)
        pspace_total *= MC_CHARSET_LEN;

    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN];
    memset(pspace_up_to, 0, sizeof(pspace_up_to));

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) { fprintf(stderr, "gpu_test_markov_chain: OOM\n"); exit(-1); }
    indices[0] = (gpu_ulong)t->start;

    gpu_uint max_positions_val = (gpu_uint)model->max_positions;

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer sorted_pos0_buf = NULL, sorted_bigram_buf = NULL;
    gpu_buffer max_positions_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG(0,  hash_type_buf,      CL_RO, hash_type,       sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf,   CL_RO, MC_CHARSET,      MC_CHARSET_LEN);
    CLCREATEARG(2,  charset_len_buf,    CL_RO, charset_len_val, sizeof(charset_len_val));
    CLCREATEARG(3,  plen_min_buf,       CL_RO, plen_min,        sizeof(plen_min));
    CLCREATEARG(4,  plen_max_buf,       CL_RO, plen_max,        sizeof(plen_max));
    CLCREATEARG(5,  reduc_buf,          CL_RO, reduction_offset,sizeof(reduction_offset));
    CLCREATEARG(6,  chain_len_buf,      CL_RO, chain_len_val,   sizeof(chain_len_val));
    CLCREATEARG_ARRAY(7, indices_buf,   CL_RW, indices,         sizeof(gpu_ulong));
    CLCREATEARG(8,  pos_start_buf,      CL_RO, pos_start,       sizeof(pos_start));
    CLCREATEARG_ARRAY(9, pspace_up_to_buf, CL_RO, pspace_up_to,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_ulong));
    CLCREATEARG(10, pspace_total_buf,   CL_RO, pspace_total,    sizeof(pspace_total));
    CLCREATEARG_ARRAY(11, sorted_pos0_buf, CL_RO, model->sorted_pos0,
                      MC_CHARSET_LEN * sizeof(uint8_t));
    CLCREATEARG_ARRAY(12, sorted_bigram_buf, CL_RO, model->sorted_bigram,
                      model->max_positions * MC_CHARSET_LEN * MC_CHARSET_LEN * sizeof(uint8_t));
    CLCREATEARG(13, max_positions_buf, CL_RO, max_positions_val, sizeof(max_positions_val));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU Markov chain error:\n"
               "\tStart:        %"PRIu64"\n"
               "\tExpected end: %"PRIu64"\n"
               "\tComputed end: %"PRIu64"\n\n",
               t->start, expected_end, (uint64_t)indices[0]);
    }

    CLFREEBUFFER(hash_type_buf);
    CLFREEBUFFER(charset_buf);
    CLFREEBUFFER(charset_len_buf);
    CLFREEBUFFER(plen_min_buf);
    CLFREEBUFFER(plen_max_buf);
    CLFREEBUFFER(reduc_buf);
    CLFREEBUFFER(chain_len_buf);
    CLFREEBUFFER(indices_buf);
    CLFREEBUFFER(pos_start_buf);
    CLFREEBUFFER(pspace_up_to_buf);
    CLFREEBUFFER(pspace_total_buf);
    CLFREEBUFFER(sorted_pos0_buf);
    CLFREEBUFFER(sorted_bigram_buf);
    CLFREEBUFFER(max_positions_buf);
    CLRELEASEQUEUE(queue);

    FREE(indices);
    return test_passed;
}


int test_chain_markov(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(markov_chain_tests) /
                                            sizeof(markov_chain_tests[0]));

    /* Build synthetic model with max_positions=1 (single bigram table) */
    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {5, 15, 10,
                                1,  1, 50,
                                8,  2,  4};

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = MC_CHARSET_LEN;
    m.max_positions = 1;
    memcpy(m.charset, MC_CHARSET, MC_CHARSET_LEN);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(MC_CHARSET_LEN * sizeof(uint8_t));
    m.sorted_bigram = malloc(m.max_positions * MC_CHARSET_LEN * MC_CHARSET_LEN * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "test_chain_markov: OOM\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }
    markov_build_sorted(&m);

    for (i = 0; i < num_tests; i++) {
        const struct markov_chain_test *t = &markov_chain_tests[i];
        uint64_t cpu_end = cpu_markov_chain(t->start, t->chain_len,
                                             t->table_index, &m, MC_PLAINTEXT_LEN);
        tests_passed &= gpu_test_markov_chain(device, context, kernel,
                                               t, cpu_end, &m, MC_PLAINTEXT_LEN);
    }

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return tests_passed;
}
