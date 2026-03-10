/*
 * Rainbow Crackalack: test_chain_mask.c
 * GPU chain tests for mask attack using the generic crackalack kernel
 * with is_mask=1.
 *
 * Strategy: compute the expected chain endpoint via an inline CPU reference
 * (index_to_plaintext_mask -> ntlm_hash -> hash_to_index), then verify the
 * GPU crackalack kernel (with is_mask=1) produces the same endpoint.
 */

#include <inttypes.h>
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

struct mask_chain_test {
    const char   *mask_str;
    uint64_t      start;
    unsigned int  chain_len;
    unsigned int  table_index;
};

static struct mask_chain_test mask_chain_tests[] = {
    /* ?d?d: small keyspace (100), deterministic */
    {"?d?d",   0, 100, 0},
    {"?d?d",  42, 100, 0},
    {"?d?d",   0, 200, 1},

    /* ?l?d: medium keyspace (260) */
    {"?l?d",   0, 100, 0},
    {"?l?d", 100, 100, 1},

    /* ?u?l?d: larger keyspace (6760) */
    {"?u?l?d",  0, 100, 0},

    /* chain_len=1: 0 steps, end must equal start */
    {"?d?d",    7,   1, 0},
};


/*
 * CPU reference mask chain generator.
 * Walks chain_len-1 steps: index_to_plaintext_mask -> ntlm_hash -> hash_to_index.
 */
static uint64_t cpu_mask_chain(const struct mask_chain_test *t)
{
    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    uint64_t pspace[MAX_PLAINTEXT_LEN + 1];

    if (mask_parse(t->mask_str, &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "cpu_mask_chain: mask_parse failed for \"%s\"\n", t->mask_str);
        return UINT64_MAX;
    }
    mask_to_gpu_buffers(&m, mask_data, mask_lens);
    fill_plaintext_space_table_mask(mask_lens, (unsigned int)m.length, pspace);

    uint64_t pspace_total = mask_keyspace(&m);
    uint64_t index = t->start;
    unsigned int reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);

    for (unsigned int pos = 0; pos < t->chain_len - 1; pos++) {
        char plaintext[MAX_PLAINTEXT_LEN + 1];
        unsigned int plaintext_len = 0;
        unsigned char hash[16];

        memset(plaintext, 0, sizeof(plaintext));
        index_to_plaintext_mask(index, mask_lens, mask_data, (unsigned int)m.length,
                                 pspace, plaintext, &plaintext_len);
        ntlm_hash(plaintext, plaintext_len, hash);
        index = hash_to_index(hash, 16, reduction_offset, pspace_total, pos);
    }
    return index;
}


/*
 * GPU mask chain test using the generic crackalack kernel.
 *
 * crackalack arg layout (same as test_chain_netntlmv1.c):
 *   0  hash_type
 *   1  charset (dummy, ignored when is_mask=1)
 *   2  charset_len (dummy)
 *   3  plaintext_len_min
 *   4  plaintext_len_max
 *   5  reduction_offset
 *   6  chain_len
 *   7  g_indices (RW)
 *   8  pos_start
 *   9  plaintext_space_up_to_index
 *  10  plaintext_space_total
 *  11  is_mask
 *  12  mask_charset_data
 *  13  mask_charset_lens
 */
static int gpu_test_mask_chain(gpu_device device, gpu_context context,
                                gpu_kernel kernel,
                                const struct mask_chain_test *t,
                                uint64_t expected_end)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    uint64_t cpu_pspace[MAX_PLAINTEXT_LEN + 1];

    if (mask_parse(t->mask_str, &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "gpu_test_mask_chain: mask_parse failed for \"%s\"\n", t->mask_str);
        return 0;
    }
    mask_to_gpu_buffers(&m, mask_data, mask_lens);
    fill_plaintext_space_table_mask(mask_lens, (unsigned int)m.length, cpu_pspace);

    gpu_uint hash_type        = HASH_NTLM;
    gpu_uint charset_len_val  = 1;
    char dummy_charset[1]     = {0};
    gpu_uint plen_min         = (gpu_uint)m.length;
    gpu_uint plen_max         = (gpu_uint)m.length;
    gpu_uint reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);
    gpu_uint chain_len_val    = (gpu_uint)t->chain_len;
    gpu_uint pos_start        = 0;
    gpu_uint is_mask          = 1;
    gpu_ulong pspace_total    = (gpu_ulong)mask_keyspace(&m);

    /* Build gpu_ulong pspace array for the GPU */
    gpu_ulong pspace_up_to[MAX_PLAINTEXT_LEN];
    memset(pspace_up_to, 0, sizeof(pspace_up_to));
    for (int k = 0; k < MAX_PLAINTEXT_LEN; k++)
        pspace_up_to[k] = (gpu_ulong)cpu_pspace[k];

    gpu_ulong *indices = calloc(1, sizeof(gpu_ulong));
    if (!indices) { fprintf(stderr, "gpu_test_mask_chain: OOM\n"); exit(-1); }
    indices[0] = (gpu_ulong)t->start;

    gpu_buffer hash_type_buf = NULL, charset_buf = NULL, charset_len_buf = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf = NULL;
    gpu_buffer reduc_buf = NULL, chain_len_buf = NULL;
    gpu_buffer indices_buf = NULL, pos_start_buf = NULL;
    gpu_buffer pspace_up_to_buf = NULL, pspace_total_buf = NULL;
    gpu_buffer is_mask_buf = NULL, mask_data_buf = NULL, mask_lens_buf = NULL;

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG(0,  hash_type_buf,      CL_RO, hash_type,       sizeof(hash_type));
    CLCREATEARG_ARRAY(1, charset_buf,   CL_RO, dummy_charset,   1);
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
    CLCREATEARG(11, is_mask_buf,        CL_RO, is_mask,         sizeof(is_mask));
    CLCREATEARG_ARRAY(12, mask_data_buf, CL_RO, mask_data,
                      MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN);
    CLCREATEARG_ARRAY(13, mask_lens_buf, CL_RO, mask_lens,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_uint));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(indices_buf, sizeof(gpu_ulong), indices);

    if (indices[0] == expected_end) {
        test_passed = 1;
    } else {
        printf("\n\nGPU mask chain error: mask=%s\n"
               "\tStart:        %"PRIu64"\n"
               "\tExpected end: %"PRIu64"\n"
               "\tComputed end: %"PRIu64"\n\n",
               t->mask_str, t->start, expected_end, (uint64_t)indices[0]);
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
    CLFREEBUFFER(is_mask_buf);
    CLFREEBUFFER(mask_data_buf);
    CLFREEBUFFER(mask_lens_buf);
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

    for (i = 0; i < num_tests; i++) {
        const struct mask_chain_test *t = &mask_chain_tests[i];
        uint64_t cpu_end = cpu_mask_chain(t);
        if (cpu_end == UINT64_MAX) {
            /* mask_parse failure already printed */
            tests_passed = 0;
            continue;
        }
        tests_passed &= gpu_test_mask_chain(device, context, kernel, t, cpu_end);
    }
    return tests_passed;
}
