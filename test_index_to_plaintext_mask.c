/*
 * Rainbow Crackalack: test_index_to_plaintext_mask.c
 * GPU unit tests for index_to_plaintext in mask mode (is_mask=1).
 *
 * For each test vector the host code parses the mask string, builds GPU
 * buffers, calls both the CPU reference (index_to_plaintext_mask) and the
 * GPU kernel, then compares the results.
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
#include "test_index_to_plaintext_mask.h"

struct i2p_mask_test {
    const char   *mask_str;
    uint64_t      index;
    const char   *expected;
    unsigned int  expected_len;
};

/*
 * Test vectors: a subset of the masks already validated by the CPU-only
 * test_mask.c group_d tests, now verified on the GPU as well.
 */
static struct i2p_mask_test mask_i2p_tests[] = {
    /* ?d (10 items) */
    {"?d",   0,  "0", 1},
    {"?d",   5,  "5", 1},
    {"?d",   9,  "9", 1},

    /* ?d?d (100 items) */
    {"?d?d",  0, "00", 2},
    {"?d?d",  1, "01", 2},
    {"?d?d", 42, "42", 2},
    {"?d?d", 99, "99", 2},

    /* ?l?d (260 items) */
    {"?l?d",   0, "a0", 2},
    {"?l?d",  26, "c6", 2},
    {"?l?d", 259, "z9", 2},

    /* ?u?l?d (6760 items) */
    {"?u?l?d",    0, "Aa0", 3},
    {"?u?l?d", 6759, "Zz9", 3},

    /* ?a (95 items) */
    {"?a",  0, "a",  1},
    {"?a", 94, "~",  1},

    /* ?d?d?d?d (10000 items) */
    {"?d?d?d?d",    0, "0000", 4},
    {"?d?d?d?d", 9999, "9999", 4},
};


/*
 * Build mask buffers from mask string, call CPU reference, compare.
 */
static int cpu_test_i2p_mask(const struct i2p_mask_test *t)
{
    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];
    uint64_t pspace[MAX_PLAINTEXT_LEN + 1];
    char plaintext[MAX_PLAINTEXT_LEN + 1];
    unsigned int plaintext_len = 0;

    if (mask_parse(t->mask_str, &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "CPU mask i2p: mask_parse failed for \"%s\"\n", t->mask_str);
        return 0;
    }
    mask_to_gpu_buffers(&m, mask_data, mask_lens);
    fill_plaintext_space_table_mask(mask_lens, (unsigned int)m.length, pspace);
    memset(plaintext, 0, sizeof(plaintext));
    index_to_plaintext_mask(t->index, mask_lens, mask_data, (unsigned int)m.length,
                             pspace, plaintext, &plaintext_len);

    if (plaintext_len == t->expected_len &&
        memcmp(plaintext, t->expected, t->expected_len) == 0)
        return 1;

    fprintf(stderr,
            "CPU mask i2p error: mask=%s index=%"PRIu64" expected=[%.*s] got=[%.*s]\n",
            t->mask_str, t->index,
            (int)t->expected_len, t->expected,
            (int)plaintext_len, plaintext);
    return 0;
}


/*
 * GPU mask i2p test.
 *
 * The kernel (test_index_to_plaintext_mask) is structurally identical to
 * test_index_to_plaintext.  For mask mode we pass:
 *   - charset = "\x00" (dummy, 1 byte), charset_len = 1
 *   - plaintext_len_min = plaintext_len_max = mask_length
 *   - is_mask = 1
 *   - mask_charset_data / mask_charset_lens from mask_to_gpu_buffers()
 *
 * Because charset_len=1 and min==max==mask_length, fill_plaintext_space_table
 * inside the kernel yields pspace[mask_length-1]=0, so index_x=index and
 * length determination always produces mask_length.  The mask decoding then
 * uses only mask_charset_data and mask_charset_lens (not charset).
 */
static int gpu_test_i2p_mask(gpu_device device, gpu_context context,
                               gpu_kernel kernel,
                               const struct i2p_mask_test *t)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    Mask m;
    char mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN];
    unsigned int mask_lens[MAX_PLAINTEXT_LEN];

    if (mask_parse(t->mask_str, &m, NULL, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "GPU mask i2p: mask_parse failed for \"%s\"\n", t->mask_str);
        return 0;
    }
    mask_to_gpu_buffers(&m, mask_data, mask_lens);

    gpu_uint charset_len_val = 1;
    char dummy_charset[1]    = {0};
    gpu_uint plen_min        = (gpu_uint)m.length;
    gpu_uint plen_max        = (gpu_uint)m.length;
    gpu_ulong index_val      = (gpu_ulong)t->index;
    gpu_uint is_mask         = 1;
    gpu_uint plaintext_len   = (gpu_uint)m.length;

    gpu_buffer charset_buf  = NULL, charset_len_buf  = NULL;
    gpu_buffer plen_min_buf = NULL, plen_max_buf     = NULL;
    gpu_buffer index_buf    = NULL, plaintext_buf    = NULL;
    gpu_buffer plen_out_buf = NULL, debug_buf        = NULL;
    gpu_buffer is_mask_buf  = NULL, mask_data_buf    = NULL, mask_lens_buf = NULL;

    unsigned char *plaintext = NULL;
    unsigned char *debug_ptr = NULL;

    queue = CLCREATEQUEUE(context, device);

    plaintext = calloc(MAX_PLAINTEXT_LEN, sizeof(unsigned char));
    if (!plaintext) { fprintf(stderr, "gpu_test_i2p_mask: OOM\n"); exit(-1); }

    CLCREATEARG_ARRAY(0, charset_buf,    CL_RO, dummy_charset,    1);
    CLCREATEARG(1, charset_len_buf,      CL_RO, charset_len_val,  sizeof(gpu_uint));
    CLCREATEARG(2, plen_min_buf,         CL_RO, plen_min,         sizeof(gpu_uint));
    CLCREATEARG(3, plen_max_buf,         CL_RO, plen_max,         sizeof(gpu_uint));
    CLCREATEARG(4, index_buf,            CL_RO, index_val,        sizeof(gpu_ulong));
    CLCREATEARG_ARRAY(5, plaintext_buf,  CL_WO, plaintext,        MAX_PLAINTEXT_LEN);
    CLCREATEARG(6, plen_out_buf,         CL_WO, plaintext_len,    sizeof(gpu_uint));
    CLCREATEARG_DEBUG(7, debug_buf, debug_ptr);
    CLCREATEARG(8, is_mask_buf,          CL_RO, is_mask,          sizeof(gpu_uint));
    CLCREATEARG_ARRAY(9, mask_data_buf,  CL_RO, mask_data,
                      MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN);
    CLCREATEARG_ARRAY(10, mask_lens_buf, CL_RO, mask_lens,
                      MAX_PLAINTEXT_LEN * sizeof(gpu_uint));

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(plaintext_buf, MAX_PLAINTEXT_LEN, plaintext);
    CLREADBUFFER(plen_out_buf,  sizeof(gpu_uint),  &plaintext_len);

    if (plaintext_len == (gpu_uint)t->expected_len &&
        memcmp(plaintext, t->expected, t->expected_len) == 0) {
        test_passed = 1;
    } else {
        fprintf(stderr,
                "GPU mask i2p error: mask=%s index=%"PRIu64" "
                "expected=[%.*s] got=[%.*s] (plen_out=%u)\n",
                t->mask_str, t->index,
                (int)t->expected_len, t->expected,
                (int)plaintext_len, (char *)plaintext, plaintext_len);
    }

    CLFREEBUFFER(charset_buf);
    CLFREEBUFFER(charset_len_buf);
    CLFREEBUFFER(plen_min_buf);
    CLFREEBUFFER(plen_max_buf);
    CLFREEBUFFER(index_buf);
    CLFREEBUFFER(plaintext_buf);
    CLFREEBUFFER(plen_out_buf);
    CLFREEBUFFER(debug_buf);
    CLFREEBUFFER(is_mask_buf);
    CLFREEBUFFER(mask_data_buf);
    CLFREEBUFFER(mask_lens_buf);
    CLRELEASEQUEUE(queue);

    FREE(plaintext);
    FREE(debug_ptr);
    return test_passed;
}


int test_index_to_plaintext_mask(gpu_device device, gpu_context context,
                                  gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(mask_i2p_tests) /
                                            sizeof(mask_i2p_tests[0]));

    for (i = 0; i < num_tests; i++) {
        tests_passed &= cpu_test_i2p_mask(&mask_i2p_tests[i]);
        tests_passed &= gpu_test_i2p_mask(device, context, kernel,
                                           &mask_i2p_tests[i]);
    }
    return tests_passed;
}
