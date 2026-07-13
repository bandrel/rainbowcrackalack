/*
 * Rainbow Crackalack: test_chain_markov_mask.c
 * GPU chain parity test for the combined mask+Markov attack using the
 * crackalack_markov_mask kernel.
 *
 * Strategy: compute the expected chain endpoint with a CPU reference
 * (index_to_plaintext_markov_mask_cpu -> ntlm_hash -> hash_to_index), then
 * verify the GPU crackalack_markov_mask kernel produces the same endpoint for
 * a handful of start indices / chain lengths / table indices.
 *
 * The kernel dispatched here is the production crackalack_markov_mask kernel
 * (loaded by crackalack_unit_tests.c via load_kernel), mirroring how
 * test_chain_markov.c exercises the production crackalack_markov kernel.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "markov.h"
#include "mask_parse.h"
#include "markov_mask.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_chain_markov_mask.h"

/*
 * Synthetic model over charset "abc" (len=3), mask "?1?1" with ?1='abc',
 * so mask_len=2, sizes={3,3}, keyspace = 9.
 */
static const char MM_CHARSET[]            = "abc";
static const unsigned int MM_CHARSET_LEN  = 3;

struct mm_chain_test {
    uint64_t     start;
    unsigned int chain_len;
    unsigned int table_index;
};

static struct mm_chain_test mm_chain_tests[] = {
    {0UL,  100,  0},
    {1UL,  100,  0},
    {5UL,  200,  1},
    {8UL,  100,  0},
    /* chain_len=1: loop runs 0 iterations, end must equal start */
    {3UL,    1,  0},
};


/*
 * CPU reference mask+Markov chain generator.
 * Walks chain_len-1 steps: i2p_markov_mask -> ntlm_hash -> hash_to_index.
 */
static uint64_t cpu_markov_mask_chain(uint64_t start, unsigned int chain_len,
                                      unsigned int table_index,
                                      const markov_mask_tables *t,
                                      uint64_t pspace_total)
{
    uint64_t index = start;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);
    unsigned int pos;

    for (pos = 0; pos < chain_len - 1; pos++) {
        unsigned char plaintext[MAX_PLAINTEXT_LEN];
        unsigned char hash[16];
        unsigned int plaintext_len = 0;

        memset(plaintext, 0, sizeof(plaintext));
        index_to_plaintext_markov_mask_cpu(t, index, plaintext, &plaintext_len);
        ntlm_hash((char *)plaintext, plaintext_len, hash);
        index = hash_to_index(hash, 16, reduction_offset, pspace_total, pos);
    }
    return index;
}


/*
 * GPU test: call crackalack_markov_mask kernel and compare endpoint.
 *
 * crackalack_markov_mask arg layout:
 *   0  hash_type
 *   1  charset
 *   2  charset_len
 *   3  plaintext_len_min (unused but must be present)
 *   4  plaintext_len_max
 *   5  reduction_offset
 *   6  chain_len
 *   7  g_indices (RW)
 *   8  pos_start
 *   9  plaintext_space_up_to_index (unused)
 *  10  plaintext_space_total
 *  11  r_pos0    (constant)
 *  12  r_bigram  (constant)
 *  13  sizes     (constant)
 *  14  mask_len
 *  15  max_sz
 */
static int gpu_test_markov_mask_chain(gpu_device device, gpu_context context,
                                      gpu_kernel kernel,
                                      uint64_t start, unsigned int chain_len,
                                      unsigned int table_index,
                                      uint64_t pspace_total_val,
                                      uint64_t expected_end,
                                      const markov_mask_tables *t)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_uint hash_type         = HASH_NTLM;
    gpu_uint charset_len_val   = t->charset_len;
    gpu_uint plen_min          = (gpu_uint)t->mask_len;
    gpu_uint plen_max          = (gpu_uint)t->mask_len;
    gpu_uint reduction_offset  = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);
    gpu_uint chain_len_val     = (gpu_uint)chain_len;
    gpu_uint pos_start         = 0;
    gpu_uint mask_len_val      = (gpu_uint)t->mask_len;
    gpu_uint max_sz_val        = (gpu_uint)t->max_sz;

    gpu_ulong pspace_total = (gpu_ulong)pspace_total_val;

    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN];
    memset(pspace_up_to, 0, sizeof(pspace_up_to));

    /* sizes as fixed-size uint array (MAX_PLAINTEXT_LEN), zero-padded. */
    gpu_uint sizes_flat[MAX_PLAINTEXT_LEN];
    memset(sizes_flat, 0, sizeof(sizes_flat));
    for (unsigned int i = 0; i < t->mask_len; i++)
        sizes_flat[i] = (gpu_uint)t->sizes[i];

    /* Flatten restricted tables to GPU buffers. */
    uint8_t r_pos0_flat[256];
    size_t rb_len = (size_t)t->mask_len * t->charset_len * t->max_sz;
    uint8_t *r_bigram_flat = calloc(rb_len ? rb_len : 1, sizeof(uint8_t));
    if (!r_bigram_flat) { fprintf(stderr, "gpu_test_markov_mask_chain: OOM\n"); exit(-1); }
    markov_mask_tables_to_gpu_buffers(t, r_pos0_flat, r_bigram_flat);

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) { fprintf(stderr, "gpu_test_markov_mask_chain: OOM\n"); exit(-1); }
    indices[0] = (gpu_ulong)start;

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer r_pos0_buf = NULL, r_bigram_buf = NULL, sizes_buf = NULL;
    gpu_buffer mask_len_buf = NULL, max_sz_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG(0,  hash_type_buf,      CL_RO, hash_type,       sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf,   CL_RO, t->charset,      t->charset_len);
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
    CLCREATEARG_ARRAY(11, r_pos0_buf,   CL_RO, r_pos0_flat,     256 * sizeof(uint8_t));
    CLCREATEARG_ARRAY(12, r_bigram_buf, CL_RO, r_bigram_flat,   rb_len * sizeof(uint8_t));
    CLCREATEARG_ARRAY(13, sizes_buf,    CL_RO, sizes_flat,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_uint));
    CLCREATEARG(14, mask_len_buf,       CL_RO, mask_len_val,    sizeof(mask_len_val));
    CLCREATEARG(15, max_sz_buf,         CL_RO, max_sz_val,      sizeof(max_sz_val));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == (gpu_ulong)expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU Markov+mask chain error:\n"
               "\tStart:        %"PRIu64"\n"
               "\tPspace:       %"PRIu64"\n"
               "\tExpected end: %"PRIu64"\n"
               "\tComputed end: %"PRIu64"\n\n",
               start, pspace_total_val, expected_end, (uint64_t)indices[0]);
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
    CLFREEBUFFER(r_pos0_buf);
    CLFREEBUFFER(r_bigram_buf);
    CLFREEBUFFER(sizes_buf);
    CLFREEBUFFER(mask_len_buf);
    CLFREEBUFFER(max_sz_buf);
    CLRELEASEQUEUE(queue);

    FREE(indices);
    FREE(r_bigram_flat);
    return test_passed;
}


int test_chain_markov_mask(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(mm_chain_tests) /
                                            sizeof(mm_chain_tests[0]));

    /* Build synthetic model over "abc" with max_positions=1. */
    uint64_t pos0_freq[3]   = {10, 30, 20};
    uint64_t bigram_freq[9] = {5, 15, 10,
                                1,  1, 50,
                                8,  2,  4};

    markov_model m;
    memset(&m, 0, sizeof(m));
    m.charset_len   = MM_CHARSET_LEN;
    m.max_positions = 1;
    memcpy(m.charset, MM_CHARSET, MM_CHARSET_LEN);
    m.pos0_freq     = pos0_freq;
    m.bigram_freq   = bigram_freq;
    m.sorted_pos0   = malloc(MM_CHARSET_LEN * sizeof(uint8_t));
    m.sorted_bigram = malloc(m.max_positions * MM_CHARSET_LEN * MM_CHARSET_LEN * sizeof(uint8_t));

    if (!m.sorted_pos0 || !m.sorted_bigram) {
        fprintf(stderr, "test_chain_markov_mask: OOM\n");
        free(m.sorted_pos0);
        free(m.sorted_bigram);
        return 0;
    }
    markov_build_sorted(&m);

    /* Mask "?1?1" with ?1='abc' -> sizes {3,3}, keyspace 9. */
    Mask mask;
    if (mask_parse("?1?1", &mask, "abc", NULL, NULL, NULL) != 0) {
        fprintf(stderr, "test_chain_markov_mask: mask_parse failed\n");
        m.pos0_freq = NULL; m.bigram_freq = NULL; markov_free(&m);
        return 0;
    }

    markov_mask_tables t;
    if (markov_build_restricted(&m, &mask, &t) != 0) {
        fprintf(stderr, "test_chain_markov_mask: markov_build_restricted failed\n");
        m.pos0_freq = NULL; m.bigram_freq = NULL; markov_free(&m);
        return 0;
    }

    uint64_t keyspace = markov_mask_keyspace(&t);  /* == 9 */

    /* Full-keyspace tests. */
    for (i = 0; i < num_tests; i++) {
        const struct mm_chain_test *tc = &mm_chain_tests[i];
        uint64_t cpu_end = cpu_markov_mask_chain(tc->start, tc->chain_len,
                                                 tc->table_index, &t, keyspace);
        tests_passed &= gpu_test_markov_mask_chain(device, context, kernel,
                                                   tc->start, tc->chain_len,
                                                   tc->table_index, keyspace,
                                                   cpu_end, &t);
    }

    /* Truncated-keyspace tests: pspace_total < keyspace. */
    {
        uint64_t truncated_pspace = 5;
        uint64_t starts[]         = {0, 1, 3, 4};
        unsigned int chain_lens[] = {50, 50, 100, 50};

        for (i = 0; i < 4; i++) {
            uint64_t cpu_end = cpu_markov_mask_chain(starts[i], chain_lens[i],
                                                     0, &t, truncated_pspace);
            tests_passed &= gpu_test_markov_mask_chain(device, context, kernel,
                                                       starts[i], chain_lens[i],
                                                       0, truncated_pspace,
                                                       cpu_end, &t);
        }
    }

    markov_mask_tables_free(&t);
    m.pos0_freq   = NULL;
    m.bigram_freq = NULL;
    markov_free(&m);

    return tests_passed;
}
