/*
 * Rainbow Crackalack: test_index_to_plaintext_mask.c
 * GPU unit tests for index_to_plaintext_mask kernel.
 *
 * Parses a fixed hashcat-style mask, flattens it into GPU buffers via
 * mask_to_gpu_buffers(), then for a spread of indices verifies that the
 * GPU kernel produces the same plaintext as the CPU reference
 * index_to_plaintext_mask_cpu().
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
#include "test_index_to_plaintext_mask.h"

/*
 * Test mask: "?u?l?l?d?d?d?d"  (length 7)
 *   pos 0: ?u  = A-Z           (26 chars)
 *   pos 1: ?l  = a-z           (26 chars)
 *   pos 2: ?l  = a-z           (26 chars)
 *   pos 3: ?d  = 0-9           (10 chars)
 *   pos 4: ?d  = 0-9           (10 chars)
 *   pos 5: ?d  = 0-9           (10 chars)
 *   pos 6: ?d  = 0-9           (10 chars)
 *
 * Keyspace = 26 * 26 * 26 * 10 * 10 * 10 * 10 = 175,760,000
 */
#define TEST_MASK_STR "?u?l?l?d?d?d?d"

static int gpu_test_i2p_mask(gpu_device device, gpu_context context,
                              gpu_kernel kernel,
                              const char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN],
                              const unsigned int mask_lens[MAX_PLAINTEXT_LEN],
                              unsigned int mask_len,
                              uint64_t index,
                              const char *expected,
                              unsigned int expected_len)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer mask_data_buf    = NULL;
    gpu_buffer mask_lens_buf    = NULL;
    gpu_buffer mask_len_buf     = NULL;
    gpu_buffer index_buf        = NULL;
    gpu_buffer plaintext_buf    = NULL;
    gpu_buffer plen_out_buf     = NULL;
    gpu_buffer debug_buf        = NULL;

    unsigned char *plaintext = NULL;
    unsigned char *debug_ptr = NULL;

    gpu_uint  mask_len_val = (gpu_uint)mask_len;
    gpu_ulong index_val    = (gpu_ulong)index;
    gpu_uint  plen_out     = 0;

    queue = CLCREATEQUEUE(context, device);

    plaintext = calloc(MAX_PLAINTEXT_LEN, sizeof(unsigned char));
    if (!plaintext) { fprintf(stderr, "OOM in gpu_test_i2p_mask\n"); exit(-1); }

    /*
     * Arg slot layout (must match CUDA/test_index_to_plaintext_mask.cu):
     *   0  mask_data      - flat per-position charset, MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN bytes
     *   1  mask_lens      - per-position sizes,        MAX_PLAINTEXT_LEN * sizeof(gpu_uint)
     *   2  mask_len       - number of positions        (scalar gpu_uint)
     *   3  index          - index to decode            (scalar gpu_ulong)
     *   4  plaintext      - output plaintext           (MAX_PLAINTEXT_LEN bytes, write-only)
     *   5  plaintext_len_out - output length           (scalar gpu_uint, write-only)
     *   6  debug          - debug scratch buffer
     */
    CLCREATEARG_ARRAY(0, mask_data_buf, CL_RO, mask_data,
                      MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN);
    CLCREATEARG_ARRAY(1, mask_lens_buf, CL_RO, mask_lens,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_uint));
    CLCREATEARG(2, mask_len_buf,        CL_RO, mask_len_val,  sizeof(gpu_uint));
    CLCREATEARG(3, index_buf,           CL_RO, index_val,     sizeof(gpu_ulong));
    CLCREATEARG_ARRAY(4, plaintext_buf, CL_WO, plaintext, MAX_PLAINTEXT_LEN);
    CLCREATEARG(5, plen_out_buf,        CL_WO, plen_out,      sizeof(gpu_uint));
    CLCREATEARG_DEBUG(6, debug_buf, debug_ptr);

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(plaintext_buf, MAX_PLAINTEXT_LEN, plaintext);
    CLREADBUFFER(plen_out_buf,  sizeof(gpu_uint),  &plen_out);

    if (plen_out == expected_len &&
        memcmp(plaintext, expected, expected_len) == 0) {
        test_passed = 1;
    } else {
        fprintf(stderr,
                "GPU Mask i2p error: index=%"PRIu64" expected=[%.*s] "
                "got=[%.*s] (plen_out=%u)\n",
                index, (int)expected_len, expected,
                (int)plen_out, (char *)plaintext, plen_out);
    }

    CLFREEBUFFER(mask_data_buf);
    CLFREEBUFFER(mask_lens_buf);
    CLFREEBUFFER(mask_len_buf);
    CLFREEBUFFER(index_buf);
    CLFREEBUFFER(plaintext_buf);
    CLFREEBUFFER(plen_out_buf);
    CLFREEBUFFER(debug_buf);
    CLRELEASEQUEUE(queue);

    FREE(plaintext);
    FREE(debug_ptr);
    return test_passed;
}


int test_index_to_plaintext_mask(gpu_device device, gpu_context context,
                                  gpu_kernel kernel)
{
    int tests_passed = 1;

    Mask m;
    memset(&m, 0, sizeof(m));

    if (mask_parse(TEST_MASK_STR, &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "test_index_to_plaintext_mask: mask_parse failed\n");
        return 0;
    }

    uint64_t ks = mask_keyspace(&m);

    /* Flatten mask into GPU-ready buffers. */
    char         mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    memset(mask_data, 0, sizeof(mask_data));
    memset(mask_lens, 0, sizeof(mask_lens));
    mask_to_gpu_buffers(&m, mask_data, mask_lens);

    unsigned int mask_len = (unsigned int)m.length;

    /* Test indices: 0, 1, midpoint, last. */
    uint64_t test_indices[] = {0, 1, ks / 2, ks - 1};
    unsigned int num_tests = (unsigned int)(sizeof(test_indices) /
                                            sizeof(test_indices[0]));
    unsigned int i;

    for (i = 0; i < num_tests; i++) {
        uint64_t idx = test_indices[i];

        /* CPU oracle. */
        char     cpu_pt[MAX_PLAINTEXT_LEN + 1];
        unsigned int cpu_len = 0;
        memset(cpu_pt, 0, sizeof(cpu_pt));
        index_to_plaintext_mask_cpu(idx, &m, cpu_pt, &cpu_len);

        if (cpu_len != mask_len) {
            fprintf(stderr,
                    "test_index_to_plaintext_mask: CPU oracle returned wrong length "
                    "%u (expected %u) for index %" PRIu64 "\n",
                    cpu_len, mask_len, idx);
            tests_passed = 0;
            continue;
        }

        /* GPU kernel. */
        tests_passed &= gpu_test_i2p_mask(device, context, kernel,
                                           mask_data, mask_lens, mask_len,
                                           idx, cpu_pt, cpu_len);
    }

    return tests_passed;
}
