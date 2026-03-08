/*
 * Rainbow Crackalack: test_hash_to_index_netntlmv1.c
 * GPU and CPU hash_to_index tests for NetNTLMv1 (8-byte hashes).
 *
 * Strategy: compute the expected reduction index with the CPU hash_to_index
 * function, then verify the GPU returns the same value.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_hash_to_index_netntlmv1.h"


/* 16 hex chars = 8-byte NetNTLMv1 hash. */
struct h2i_test {
    char hash[16 + 1];
    unsigned int charset_len;
    unsigned int plaintext_len_min;
    unsigned int plaintext_len_max;
    unsigned int table_index;
    unsigned int pos;
};

static struct h2i_test netntlmv1_h2i_tests[] = {
    /* ascii-32-95, 7-char plaintexts (typical NetNTLMv1 table parameters). */
    {"aabbccddeeff0011", 95, 7, 7, 0,   0},
    {"aabbccddeeff0011", 95, 7, 7, 0, 100},
    {"deadbeefcafe1234", 95, 7, 7, 3,   0},
    {"0102030405060708", 95, 7, 7, 5, 750},
    {"ffeeddccbbaa9988", 95, 7, 7, 0,   0},
};


static int gpu_test_netntlmv1_h2i(gpu_device device, gpu_context context,
                                   gpu_kernel kernel,
                                   const struct h2i_test *t,
                                   uint64_t expected_index)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer hash_buffer = NULL, hash_len_buffer = NULL;
    gpu_buffer charset_len_buffer = NULL;
    gpu_buffer plaintext_len_min_buffer = NULL, plaintext_len_max_buffer = NULL;
    gpu_buffer table_index_buffer = NULL, pos_buffer = NULL;
    gpu_buffer index_buffer = NULL, debug_buffer = NULL;

    unsigned char hash_bytes[MAX_HASH_OUTPUT_LEN] = {0};
    unsigned int hash_len = 0;
    gpu_ulong index = 0;
    gpu_ulong *index_ptr = NULL;
    unsigned char *debug_ptr = NULL;

    queue = CLCREATEQUEUE(context, device);

    hash_len = hex_to_bytes((char *)t->hash, sizeof(hash_bytes), hash_bytes);

    CLCREATEARG_ARRAY(0, hash_buffer, CL_RO, hash_bytes, hash_len);
    CLCREATEARG(1, hash_len_buffer, CL_RO, hash_len, sizeof(hash_len));
    CLCREATEARG(2, charset_len_buffer, CL_RO, t->charset_len, sizeof(t->charset_len));
    CLCREATEARG(3, plaintext_len_min_buffer, CL_RO,
                t->plaintext_len_min, sizeof(t->plaintext_len_min));
    CLCREATEARG(4, plaintext_len_max_buffer, CL_RO,
                t->plaintext_len_max, sizeof(t->plaintext_len_max));
    CLCREATEARG(5, table_index_buffer, CL_RO, t->table_index, sizeof(t->table_index));
    CLCREATEARG(6, pos_buffer, CL_RO, t->pos, sizeof(t->pos));
    CLCREATEARG(7, index_buffer, CL_WO, index, sizeof(index));
    CLCREATEARG_DEBUG(8, debug_buffer, debug_ptr);

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    index_ptr = calloc(1, sizeof(gpu_ulong));
    if (!index_ptr) {
        fprintf(stderr, "Error allocating index buffer in test_hash_to_index_netntlmv1\n");
        exit(-1);
    }

    CLREADBUFFER(index_buffer, sizeof(gpu_ulong), index_ptr);

    if (*index_ptr == expected_index) {
        test_passed = 1;
    } else {
        printf("\n\nGPU NetNTLMv1 h2i Error:\n"
               "\tHash:           %s\n"
               "\tExpected index: %"PRIu64"\n"
               "\tComputed index: %"PRIu64"\n\n",
               t->hash, expected_index, *index_ptr);
    }

    CLFREEBUFFER(hash_buffer);
    CLFREEBUFFER(hash_len_buffer);
    CLFREEBUFFER(charset_len_buffer);
    CLFREEBUFFER(plaintext_len_min_buffer);
    CLFREEBUFFER(plaintext_len_max_buffer);
    CLFREEBUFFER(table_index_buffer);
    CLFREEBUFFER(pos_buffer);
    CLFREEBUFFER(index_buffer);
    CLFREEBUFFER(debug_buffer);
    CLRELEASEQUEUE(queue);

    FREE(index_ptr);
    FREE(debug_ptr);
    return test_passed;
}


int test_h2i_netntlmv1(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    uint64_t pspace_up_to[MAX_PLAINTEXT_LEN] = {0};
    uint64_t pspace_total = 0;
    unsigned int num_tests = (unsigned int)(sizeof(netntlmv1_h2i_tests) /
                                            sizeof(netntlmv1_h2i_tests[0]));

    for (i = 0; i < num_tests; i++) {
        const struct h2i_test *t = &netntlmv1_h2i_tests[i];
        unsigned char hash_bytes[MAX_HASH_OUTPUT_LEN] = {0};
        unsigned int hash_len;
        unsigned int reduction_offset;
        uint64_t cpu_index;

        hash_len = hex_to_bytes((char *)t->hash, sizeof(hash_bytes), hash_bytes);
        pspace_total = fill_plaintext_space_table(t->charset_len,
                                                   t->plaintext_len_min,
                                                   t->plaintext_len_max,
                                                   pspace_up_to);
        reduction_offset = TABLE_INDEX_TO_REDUCTION_OFFSET(t->table_index);

        /* CPU reference. */
        cpu_index = hash_to_index(hash_bytes, hash_len, reduction_offset,
                                   pspace_total, t->pos);

        /* GPU must match CPU. */
        tests_passed &= gpu_test_netntlmv1_h2i(device, context, kernel,
                                                t, cpu_index);

        /* Verify the CPU index is within the plaintext space. */
        if (cpu_index >= pspace_total) {
            fprintf(stderr, "NetNTLMv1 h2i CPU range check failed for "
                    "test %u: index %"PRIu64" >= pspace %"PRIu64"\n",
                    i, cpu_index, pspace_total);
            tests_passed = 0;
        }
    }

    return tests_passed;
}
