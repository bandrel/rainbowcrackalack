/*
 * Rainbow Crackalack: test_chain_markov_ntlm8.c
 * GPU chain tests for the Markov NTLM8 fast-path kernel (crackalack_markov_ntlm8).
 *
 * The NTLM8 kernel hardcodes charset_len=95 and plaintext_len=8, so we must
 * test with the full ascii-32-95 charset.  CPU reference walks chains using
 * index_to_plaintext_markov_cpu -> ntlm_hash -> hash_to_index, then the GPU
 * result is compared.
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
#include "test_chain_markov_ntlm8.h"

#define MC8_CHARSET_LEN    95
#define MC8_PLAINTEXT_LEN  8
#define MC8_MAX_POSITIONS  7
#define MC8_PSPACE_TOTAL   6634204312890625UL  /* 95^8 */

static const char MC8_CHARSET[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~";

struct markov_chain_test_ntlm8 {
    uint64_t     start;
    unsigned int chain_len;
    unsigned int table_index;
};

static struct markov_chain_test_ntlm8 ntlm8_tests[] = {
    {0UL,      100, 0},
    {1UL,      100, 0},
    {1000UL,    50, 1},
    {12345UL,  100, 0},
    /* chain_len=1: loop runs 0 iterations, end must equal start */
    {42UL,       1, 0},
};


/*
 * CPU reference Markov chain generator for 8-char NTLM.
 */
static uint64_t cpu_markov_chain_ntlm8(uint64_t start, unsigned int chain_len,
                                        unsigned int table_index,
                                        const markov_model *model)
{
    uint64_t index = start;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);

    for (unsigned int pos = 0; pos < chain_len - 1; pos++) {
        unsigned char plaintext[MAX_PLAINTEXT_LEN];
        unsigned char hash[16];

        memset(plaintext, 0, sizeof(plaintext));
        index_to_plaintext_markov_cpu(index, model, MC8_PLAINTEXT_LEN, plaintext);
        ntlm_hash((char *)plaintext, MC8_PLAINTEXT_LEN, hash);
        index = hash_to_index(hash, 16, reduction_offset, MC8_PSPACE_TOTAL, pos);
    }
    return index;
}


/*
 * GPU test: call crackalack_markov_ntlm8 kernel and compare endpoint.
 *
 * Arg layout matches the generic crackalack_markov kernel (14 args),
 * but the NTLM8 fast path ignores most of them.
 */
static int gpu_test_markov_chain_ntlm8(gpu_device device, gpu_context context,
                                        gpu_kernel kernel,
                                        const struct markov_chain_test_ntlm8 *t,
                                        uint64_t expected_end,
                                        const markov_model *model)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_uint hash_type        = HASH_NTLM;
    gpu_uint charset_len_val  = MC8_CHARSET_LEN;
    gpu_uint plen_min         = MC8_PLAINTEXT_LEN;
    gpu_uint plen_max         = MC8_PLAINTEXT_LEN;
    gpu_uint reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);
    gpu_uint chain_len_val    = (gpu_uint)t->chain_len;
    gpu_uint pos_start        = 0;
    gpu_ulong pspace_total    = MC8_PSPACE_TOTAL;

    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN];
    memset(pspace_up_to, 0, sizeof(pspace_up_to));

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) { fprintf(stderr, "gpu_test_markov_chain_ntlm8: OOM\n"); exit(-1); }
    indices[0] = (gpu_ulong)t->start;

    gpu_uint max_positions_val = MC8_MAX_POSITIONS;

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer sorted_pos0_buf = NULL, sorted_bigram_buf = NULL;
    gpu_buffer max_positions_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG(0,  hash_type_buf,      CL_RO, hash_type,        sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf,   CL_RO, MC8_CHARSET,      MC8_CHARSET_LEN);
    CLCREATEARG(2,  charset_len_buf,    CL_RO, charset_len_val,  sizeof(charset_len_val));
    CLCREATEARG(3,  plen_min_buf,       CL_RO, plen_min,         sizeof(plen_min));
    CLCREATEARG(4,  plen_max_buf,       CL_RO, plen_max,         sizeof(plen_max));
    CLCREATEARG(5,  reduc_buf,          CL_RO, reduction_offset, sizeof(reduction_offset));
    CLCREATEARG(6,  chain_len_buf,      CL_RO, chain_len_val,    sizeof(chain_len_val));
    CLCREATEARG_ARRAY(7, indices_buf,   CL_RW, indices,          sizeof(gpu_ulong));
    CLCREATEARG(8,  pos_start_buf,      CL_RO, pos_start,        sizeof(pos_start));
    CLCREATEARG_ARRAY(9, pspace_up_to_buf, CL_RO, pspace_up_to,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_ulong));
    CLCREATEARG(10, pspace_total_buf,   CL_RO, pspace_total,     sizeof(pspace_total));
    CLCREATEARG_ARRAY(11, sorted_pos0_buf, CL_RO, model->sorted_pos0,
                      MC8_CHARSET_LEN * sizeof(uint8_t));
    CLCREATEARG_ARRAY(12, sorted_bigram_buf, CL_RO, model->sorted_bigram,
                      MC8_MAX_POSITIONS * MC8_CHARSET_LEN * MC8_CHARSET_LEN * sizeof(uint8_t));
    CLCREATEARG(13, max_positions_buf,  CL_RO, max_positions_val, sizeof(max_positions_val));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == (gpu_ulong)expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU Markov NTLM8 chain error:\n"
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


int test_chain_markov_ntlm8(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(ntlm8_tests) /
                                            sizeof(ntlm8_tests[0]));

    /* Build a synthetic Markov model with charset_len=95, max_positions=7. */
    size_t bigram_count = (size_t)MC8_MAX_POSITIONS * MC8_CHARSET_LEN * MC8_CHARSET_LEN;

    uint64_t *pos0_freq   = malloc(MC8_CHARSET_LEN * sizeof(uint64_t));
    uint64_t *bigram_freq = malloc(bigram_count * sizeof(uint64_t));
    if (!pos0_freq || !bigram_freq) {
        fprintf(stderr, "test_chain_markov_ntlm8: OOM\n");
        free(pos0_freq);
        free(bigram_freq);
        return 0;
    }

    /* Fill with a simple deterministic pattern */
    for (unsigned int c = 0; c < MC8_CHARSET_LEN; c++)
        pos0_freq[c] = (uint64_t)(c + 1);
    for (size_t j = 0; j < bigram_count; j++)
        bigram_freq[j] = (uint64_t)((j % 97) + 1);

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = MC8_CHARSET_LEN;
    m.max_positions = MC8_MAX_POSITIONS;
    memcpy(m.charset, MC8_CHARSET, MC8_CHARSET_LEN);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(MC8_CHARSET_LEN * sizeof(uint8_t));
    m.sorted_bigram = malloc(bigram_count * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "test_chain_markov_ntlm8: OOM\n");
        free(pos0_freq);
        free(bigram_freq);
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }
    markov_build_sorted(&m);

    for (i = 0; i < num_tests; i++) {
        const struct markov_chain_test_ntlm8 *t = &ntlm8_tests[i];
        uint64_t cpu_end = cpu_markov_chain_ntlm8(t->start, t->chain_len,
                                                    t->table_index, &m);
        tests_passed &= gpu_test_markov_chain_ntlm8(device, context, kernel,
                                                      t, cpu_end, &m);
    }

    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    free(pos0_freq);
    free(bigram_freq);
    markov_free(&m);

    return tests_passed;
}
