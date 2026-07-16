/*
 * Rainbow Crackalack: test_chain_mask.c
 * GPU chain tests for mask-based generation using the crackalack_mask kernel.
 *
 * Strategy: compute the expected chain endpoint with an inline CPU reference
 * (index_to_plaintext_mask_cpu -> ntlm_hash -> hash_to_index), then verify
 * the GPU crackalack_mask kernel produces the same endpoint.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "mask_parse.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_chain_mask.h"

/*
 * Test mask: "?d?d?d" — three decimal digits, keyspace = 10^3 = 1000.
 * Small keyspace keeps the test fast and verifiable by hand.
 */
static const char MASK_STR[] = "?d?d?d";

struct mask_chain_test {
    uint64_t     start;
    unsigned int chain_len;
    unsigned int table_index;
};

static struct mask_chain_test mask_chain_tests[] = {
    {0UL,   100, 0},
    {1UL,   100, 0},
    {500UL, 200, 1},
    {999UL, 100, 0},
    /* chain_len=1: loop runs 0 iterations, end must equal start */
    {42UL,    1, 0},
};


/*
 * CPU reference mask chain generator.
 * Walks chain_len-1 steps: i2p_mask -> ntlm_hash -> hash_to_index.
 */
static uint64_t cpu_mask_chain(uint64_t start, unsigned int chain_len,
                                unsigned int table_index,
                                const Mask *mask)
{
    uint64_t index = start;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(table_index);
    uint64_t pspace_total = mask_keyspace(mask);
    unsigned int pos;

    for (pos = 0; pos < chain_len - 1; pos++) {
        char plaintext[MAX_PLAINTEXT_LEN + 1];
        unsigned char hash[16];
        unsigned int plaintext_len;

        memset(plaintext, 0, sizeof(plaintext));
        index_to_plaintext_mask_cpu(index, mask, plaintext, &plaintext_len);
        ntlm_hash(plaintext, plaintext_len, hash);
        index = hash_to_index(hash, 16, reduction_offset, pspace_total, pos);
    }
    return index;
}


/*
 * GPU test: call crackalack_mask kernel and compare endpoint with expected.
 *
 * crackalack_mask arg layout:
 *   0  hash_type
 *   1  charset           (unused by mask; passed for ABI compat)
 *   2  charset_len       (unused by mask; passed for ABI compat)
 *   3  plaintext_len_min (unused by mask; passed for ABI compat)
 *   4  plaintext_len_max (unused by mask; passed for ABI compat)
 *   5  reduction_offset
 *   6  chain_len
 *   7  g_indices (RW)
 *   8  pos_start
 *   9  plaintext_space_up_to_index (unused by mask; passed for ABI compat)
 *  10  plaintext_space_total
 *  11  mask_data  (flat [MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN] chars)
 *  12  mask_lens  (per-position charset sizes, MAX_PLAINTEXT_LEN uints)
 *  13  mask_len   (number of positions in the mask)
 */
static int gpu_test_mask_chain(gpu_device device, gpu_context context,
                                gpu_kernel kernel,
                                const struct mask_chain_test *t,
                                uint64_t expected_end,
                                const Mask *mask)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    /* ABI-compat placeholders for unused generic-kernel args */
    static const char DUMMY_CHARSET[] = "0123456789";
    static const unsigned int DUMMY_CHARSET_LEN = 10;

    gpu_uint hash_type         = HASH_NTLM;
    gpu_uint charset_len_val   = (gpu_uint)DUMMY_CHARSET_LEN;
    gpu_uint plen_min          = (gpu_uint)mask->length;
    gpu_uint plen_max          = (gpu_uint)mask->length;
    gpu_uint reduction_offset  = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);
    gpu_uint chain_len_val     = (gpu_uint)t->chain_len;
    gpu_uint pos_start         = 0;
    gpu_uint mask_len_val      = (gpu_uint)mask->length;

    gpu_ulong pspace_total = (gpu_ulong)mask_keyspace(mask);

    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN];
    memset(pspace_up_to, 0, sizeof(pspace_up_to));

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) { fprintf(stderr, "gpu_test_mask_chain: OOM\n"); exit(-1); }
    indices[0] = (gpu_ulong)t->start;

    /* Build flat GPU mask buffers */
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    memset(mask_data, 0, sizeof(mask_data));
    memset(mask_lens, 0, sizeof(mask_lens));
    mask_to_gpu_buffers(mask, mask_data, mask_lens);

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer mask_data_buf = NULL, mask_lens_buf = NULL;
    gpu_buffer mask_len_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG(0,  hash_type_buf,      CL_RO, hash_type,       sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf,   CL_RO, DUMMY_CHARSET,   DUMMY_CHARSET_LEN);
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
    CLCREATEARG_ARRAY(11, mask_data_buf, CL_RO, mask_data,
                      MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN * sizeof(char));
    CLCREATEARG_ARRAY(12, mask_lens_buf, CL_RO, mask_lens,
                      MAX_PLAINTEXT_LEN * sizeof(unsigned int));
    CLCREATEARG(13, mask_len_buf,       CL_RO, mask_len_val,    sizeof(mask_len_val));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == (gpu_ulong)expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU Mask chain error:\n"
               "\tMask:         %s\n"
               "\tStart:        %"PRIu64"\n"
               "\tExpected end: %"PRIu64"\n"
               "\tComputed end: %"PRIu64"\n\n",
               MASK_STR, t->start, expected_end, (uint64_t)indices[0]);
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
    CLFREEBUFFER(mask_data_buf);
    CLFREEBUFFER(mask_lens_buf);
    CLFREEBUFFER(mask_len_buf);
    CLRELEASEQUEUE(queue);

    FREE(indices);
    return test_passed;
}


int test_chain_mask(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(mask_chain_tests) /
                                            sizeof(mask_chain_tests[0]));

    Mask mask;
    memset(&mask, 0, sizeof(mask));
    if (mask_parse(MASK_STR, &mask, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "test_chain_mask: mask_parse failed for \"%s\"\n", MASK_STR);
        return 0;
    }

    for (i = 0; i < num_tests; i++) {
        const struct mask_chain_test *t = &mask_chain_tests[i];
        uint64_t cpu_end = cpu_mask_chain(t->start, t->chain_len,
                                           t->table_index, &mask);
        tests_passed &= gpu_test_mask_chain(device, context, kernel,
                                             t, cpu_end, &mask);
    }

    return tests_passed;
}
