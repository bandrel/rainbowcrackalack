/*
 * Rainbow Crackalack: test_chain_netntlmv1.c
 * GPU chain tests for the NetNTLMv1 hash using the generic crackalack kernel.
 *
 * Strategy: compute the chain end with an inline CPU reference implementation
 * (index_to_plaintext -> setup_des_key -> netntlmv1_hash -> hash_to_index),
 * then verify the GPU produces the same end point.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "charset.h"
#include "cpu_rt_functions.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_chain_netntlmv1.h"


struct netntlmv1_chain_test {
    uint64_t start;
    unsigned int chain_len;
    unsigned int table_index;
    const char *charset;
    unsigned int charset_len;
    unsigned int plaintext_len; /* min == max for NetNTLMv1 tables */
};

/* ascii-32-95 7-char chain tests.
 * Includes low table_index (0, 1) for basic coverage and high table_index
 * (100, 1000) to exercise large reduction offsets (table_index * 65536).
 * chain_len=1 is a degenerate case: 0 hash+reduce steps, end==start.
 * The high start index (69833729609374) is pspace_total-1 for 7-char
 * ascii-32-95 (95^7), exercising the upper plaintext space boundary. */
static struct netntlmv1_chain_test netntlmv1_chain_tests[] = {
    {0UL,              100,  0, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    {999UL,            100,  0, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    {0UL,              200,  1, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    {0UL,              100,  100, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    {500UL,            150,  1000, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    /* chain_len=1: loop runs 0 iterations, end must equal start */
    {42UL,               1,    0, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    /* high start near top of 7-char ascii-32-95 space (95^7 - 1) */
    {69833729609374UL, 100,    0, CHARSET_ASCII_32_95, CHARSET_ASCII_32_95_LEN, 7},
    /* digits-only, 3-char plaintexts: pspace=1000, different modular reduction */
    {0UL,               50,    0, CHARSET_NUMERIC, CHARSET_NUMERIC_LEN, 3},
    {500UL,             50,    1, CHARSET_NUMERIC, CHARSET_NUMERIC_LEN, 3},
};


/*
 * CPU reference NetNTLMv1 chain generator.
 * Mirrors the GPU crackalack kernel for HASH_NETNTLMV1 using
 * index_to_plaintext + setup_des_key + netntlmv1_hash + hash_to_index.
 *
 * The loop runs chain_len-1 iterations (not chain_len) because a rainbow chain
 * of length N has N links but only N-1 hash+reduce steps: the start point is
 * link 0, each iteration produces the next link, and the final index after
 * N-1 steps is the endpoint stored in the table.  This matches the GPU
 * crackalack kernel which also iterates from pos_start to chain_len-1.
 */
static uint64_t cpu_netntlmv1_chain(uint64_t start, unsigned int chain_len,
                                     unsigned int table_index,
                                     const char *charset,
                                     unsigned int charset_len,
                                     unsigned int plaintext_len_val)
{
    uint64_t pspace_up_to[MAX_PLAINTEXT_LEN] = {0};
    uint64_t pspace_total;
    uint64_t index = start;
    unsigned int pos;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);

    pspace_total = fill_plaintext_space_table(charset_len,
                                              plaintext_len_val,
                                              plaintext_len_val,
                                              pspace_up_to);

    for (pos = 0; pos < chain_len - 1; pos++) {
        char plaintext[MAX_PLAINTEXT_LEN] = {0};
        unsigned int pl = 0;
        unsigned char key[8] = {0};
        unsigned char hash[8] = {0};

        index_to_plaintext(index, (char *)charset, charset_len,
                           plaintext_len_val, plaintext_len_val,
                           pspace_up_to, plaintext, &pl);
        setup_des_key(plaintext, key);
        netntlmv1_hash(key, 8, hash);
        /* pos is the 0-based intra-chain step; reduction_offset carries the
         * table-level offset (table_index * 65536).  Both are separate. */
        index = hash_to_index(hash, 8, reduction_offset, pspace_total, pos);
    }

    return index;
}


/* Test a NetNTLMv1 chain using the GPU (generic crackalack kernel). */
static int gpu_test_netntlmv1_chain(gpu_device device, gpu_context context,
                                     gpu_kernel kernel,
                                     const struct netntlmv1_chain_test *t,
                                     uint64_t expected_end)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_uint hash_type = HASH_NETNTLMV1;
    gpu_uint plaintext_len_min = t->plaintext_len;
    gpu_uint plaintext_len_max = t->plaintext_len;
    gpu_uint reduction_offset =
        TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);
    gpu_uint chain_len = t->chain_len;
    gpu_uint pos_start = 0;
    gpu_uint is_mask = 0;

    /* plaintext_space_up_to_index and plaintext_space_total for the GPU. */
    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN] = {0};
    gpu_ulong pspace_total = 0;
    {
        uint64_t tmp[MAX_PLAINTEXT_LEN] = {0};
        uint64_t tot = fill_plaintext_space_table(t->charset_len,
                                                   t->plaintext_len,
                                                   t->plaintext_len, tmp);
        int k;
        for (k = 0; k < MAX_PLAINTEXT_LEN; k++)
            pspace_up_to[k] = (gpu_ulong)tmp[k];
        pspace_total = (gpu_ulong)tot;
    }

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) {
        fprintf(stderr, "Error allocating indices in test_chain_netntlmv1\n");
        exit(-1);
    }
    indices[0] = (gpu_ulong)t->start;

    /* Dummy mask buffers (is_mask=0 so they are unused). */
    char dummy_mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN] = {0};
    gpu_uint dummy_mask_lens[MAX_PLAINTEXT_LEN] = {0};

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer len_min_buf = NULL, len_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer is_mask_buf = NULL;
    gpu_buffer mask_data_buf = NULL, mask_lens_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    gpu_uint charset_len_val = t->charset_len;
    CLCREATEARG(0, hash_type_buf, CL_RO, hash_type, sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf, CL_RO,
                      t->charset, t->charset_len);
    CLCREATEARG(2, charset_len_buf, CL_RO, charset_len_val, sizeof(gpu_uint));
    CLCREATEARG(3, len_min_buf, CL_RO, plaintext_len_min, sizeof(plaintext_len_min));
    CLCREATEARG(4, len_max_buf, CL_RO, plaintext_len_max, sizeof(plaintext_len_max));
    CLCREATEARG(5, reduc_buf, CL_RO, reduction_offset, sizeof(reduction_offset));
    CLCREATEARG(6, chain_len_buf, CL_RO, chain_len, sizeof(chain_len));
    CLCREATEARG_ARRAY(7, indices_buf, CL_RW, indices, sizeof(gpu_ulong));
    CLCREATEARG(8, pos_start_buf, CL_RO, pos_start, sizeof(pos_start));
    CLCREATEARG_ARRAY(9, pspace_up_to_buf, CL_RO,
                      pspace_up_to, MAX_PLAINTEXT_LEN * sizeof(gpu_ulong));
    CLCREATEARG(10, pspace_total_buf, CL_RO, pspace_total, sizeof(pspace_total));
    CLCREATEARG(11, is_mask_buf, CL_RO, is_mask, sizeof(is_mask));
    CLCREATEARG_ARRAY(12, mask_data_buf, CL_RO,
                      dummy_mask_data, sizeof(dummy_mask_data));
    CLCREATEARG_ARRAY(13, mask_lens_buf, CL_RO,
                      dummy_mask_lens, sizeof(dummy_mask_lens));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU NetNTLMv1 chain error:\n"
               "\tStart:        %"PRIu64"\n"
               "\tExpected end: %"PRIu64"\n"
               "\tComputed end: %"PRIu64"\n\n",
               t->start, expected_end, (uint64_t)indices[0]);
    }

    CLFREEBUFFER(hash_type_buf);
    CLFREEBUFFER(charset_buf);
    CLFREEBUFFER(charset_len_buf);
    CLFREEBUFFER(len_min_buf);
    CLFREEBUFFER(len_max_buf);
    CLFREEBUFFER(reduc_buf);
    CLFREEBUFFER(chain_len_buf);
    CLFREEBUFFER(indices_buf);
    CLFREEBUFFER(pos_start_buf);
    CLFREEBUFFER(pspace_up_to_buf);
    CLFREEBUFFER(pspace_total_buf);
    CLFREEBUFFER(is_mask_buf);
    CLFREEBUFFER(mask_data_buf);
    CLFREEBUFFER(mask_lens_buf);
    CLRELEASEQUEUE(queue);

    FREE(indices);
    return test_passed;
}


int test_chain_netntlmv1(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(netntlmv1_chain_tests) /
                                            sizeof(netntlmv1_chain_tests[0]));

    for (i = 0; i < num_tests; i++) {
        const struct netntlmv1_chain_test *t = &netntlmv1_chain_tests[i];
        uint64_t cpu_end = cpu_netntlmv1_chain(t->start, t->chain_len,
                                                t->table_index,
                                                t->charset, t->charset_len,
                                                t->plaintext_len);
        tests_passed &= gpu_test_netntlmv1_chain(device, context, kernel,
                                                  t, cpu_end);
    }

    return tests_passed;
}
