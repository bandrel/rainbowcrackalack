/*
 * Rainbow Crackalack: test_hash_md5.c
 * GPU and CPU hash tests for the MD5 hash.
 *
 * Strategy: compare GPU MD5 output against known-good test vectors.
 * Three standard vectors are used: empty string, "abc", and "password".
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_hash_md5.h"


struct md5_test_vector {
    const char *plaintext;
    unsigned int plaintext_len;
    const char *expected_hex;
};

static const struct md5_test_vector md5_test_vectors[] = {
    { "",         0, "d41d8cd98f00b204e9800998ecf8427e" },
    { "abc",      3, "900150983cd24fb0d6963f7d28e17f72" },
    { "password", 8, "5f4dcc3b5aa765d61d8327deb882cf99" },
};


/* Test a single MD5 hash on GPU, comparing against a known expected hex string. */
static int gpu_test_md5_hash(gpu_device device, gpu_context context,
                              gpu_kernel kernel,
                              const char *input,
                              unsigned int input_len,
                              const char *expected_hex)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer alg_buffer = NULL, input_buffer = NULL, input_len_buffer = NULL;
    gpu_buffer output_buffer = NULL, output_len_buffer = NULL, debug_buffer = NULL;

    char *gpu_input = NULL;
    unsigned char *output = NULL;
    unsigned char *debug_ptr = NULL;

    gpu_uint hash_type = HASH_MD5;
    gpu_uint gpu_input_len = input_len;
    gpu_uint output_len = 0;

    unsigned char expected_bytes[MAX_HASH_OUTPUT_LEN] = {0};
    unsigned int expected_len = 0;

    queue = CLCREATEQUEUE(context, device);

    output = calloc(MAX_HASH_OUTPUT_LEN, sizeof(unsigned char));
    if (!output) {
        fprintf(stderr, "Error allocating buffers in test_hash_md5\n");
        exit(-1);
    }

    /* Duplicate input so we can safely pass a writable pointer.
     * For empty string, strdup("") still gives a valid 1-byte buffer. */
    gpu_input = strdup(input);

    CLCREATEARG(0, alg_buffer, CL_RO, hash_type, sizeof(hash_type));
    CLCREATEARG_ARRAY(1, input_buffer, CL_RO, gpu_input, input_len + 1);
    CLCREATEARG(2, input_len_buffer, CL_RO, gpu_input_len, sizeof(gpu_uint));
    CLCREATEARG_ARRAY(3, output_buffer, CL_WO, output, MAX_HASH_OUTPUT_LEN);
    CLCREATEARG(4, output_len_buffer, CL_WO, output_len, sizeof(gpu_uint));
    CLCREATEARG_DEBUG(5, debug_buffer, debug_ptr);

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(output_buffer, MAX_HASH_OUTPUT_LEN, output);
    CLREADBUFFER(output_len_buffer, sizeof(gpu_uint), &output_len);

    expected_len = hex_to_bytes((char *)expected_hex,
                                sizeof(expected_bytes), expected_bytes);
    if ((expected_len == output_len) &&
        (memcmp(output, expected_bytes, expected_len) == 0)) {
        test_passed = 1;
    } else {
        int i;
        printf("\n\nGPU MD5 Error:\n\tPlaintext:     \"%s\"\n"
               "\tExpected hash: %s\n\tComputed hash: ", input, expected_hex);
        for (i = 0; i < (int)output_len; i++)
            printf("%02x", output[i]);
        printf("\n\n");
    }

    CLFREEBUFFER(alg_buffer);
    CLFREEBUFFER(input_buffer);
    CLFREEBUFFER(input_len_buffer);
    CLFREEBUFFER(output_buffer);
    CLFREEBUFFER(output_len_buffer);
    CLFREEBUFFER(debug_buffer);
    CLRELEASEQUEUE(queue);

    FREE(gpu_input);
    FREE(output);
    FREE(debug_ptr);
    return test_passed;
}


int test_hash_md5(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(md5_test_vectors) /
                                            sizeof(md5_test_vectors[0]));

    for (i = 0; i < num_tests; i++) {
        tests_passed &= gpu_test_md5_hash(device, context, kernel,
                                          md5_test_vectors[i].plaintext,
                                          md5_test_vectors[i].plaintext_len,
                                          md5_test_vectors[i].expected_hex);
    }

    return tests_passed;
}
