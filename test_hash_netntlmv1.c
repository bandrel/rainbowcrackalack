/*
 * Rainbow Crackalack: test_hash_netntlmv1.c
 * GPU and CPU hash tests for the NetNTLMv1 hash (DES-ECB).
 *
 * Strategy: compute the expected hash with the CPU reference implementation
 * (setup_des_key + netntlmv1_hash), then verify the GPU produces the same
 * 8-byte output.
 *
 * GPU kernel input contract:
 *   The GPU test_hash kernel receives the RAW 7-byte plaintext (not a
 *   DES-expanded key).  The kernel is responsible for calling setup_des_key
 *   internally to expand the 7-byte plaintext into an 8-byte DES key before
 *   performing the DES-ECB encryption.
 *
 *   The CPU reference (cpu_netntlmv1_hash) mirrors this by explicitly calling
 *   setup_des_key on the 7-byte plaintext, then passing the 8-byte expanded
 *   key to netntlmv1_hash.  Both paths must produce the same 8-byte hash.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_hash_netntlmv1.h"


/* 7-byte plaintexts used as DES keys in NetNTLMv1 tables. */
static const char *netntlmv1_test_inputs[] = {
    "abcdefg",
    "1234567",
    "ABCDEFG",
    "passwd1",
    " !#$%&(",
};


/*
 * Compute the CPU-side NetNTLMv1 hash of a 7-byte plaintext.
 * This mirrors the GPU kernel contract: the raw 7-byte plaintext is first
 * DES-expanded into an 8-byte key via setup_des_key, then hashed.
 */
static void cpu_netntlmv1_hash(const char *plaintext7, unsigned char *out_hash) {
    unsigned char key[8] = {0};

    setup_des_key((char *)plaintext7, key);
    netntlmv1_hash(key, 8, out_hash);
}


/* Test a single NetNTLMv1 hash on GPU, comparing against a precomputed
 * expected hex string (which the caller computes via CPU). */
static int gpu_test_netntlmv1_hash(gpu_device device, gpu_context context,
                                    gpu_kernel kernel,
                                    const char *input,
                                    const char *expected_hex)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer alg_buffer = NULL, input_buffer = NULL, input_len_buffer = NULL;
    gpu_buffer output_buffer = NULL, output_len_buffer = NULL, debug_buffer = NULL;

    char *gpu_input = NULL;
    unsigned char *output = NULL;
    unsigned char *debug_ptr = NULL;

    gpu_uint hash_type = HASH_NETNTLMV1;
    gpu_uint input_len = 7;
    gpu_uint output_len = 0;

    unsigned char expected_bytes[MAX_HASH_OUTPUT_LEN] = {0};
    unsigned int expected_len = 0;

    queue = CLCREATEQUEUE(context, device);

    output = calloc(MAX_HASH_OUTPUT_LEN, sizeof(unsigned char));
    if (!output) {
        fprintf(stderr, "Error allocating buffers in test_hash_netntlmv1\n");
        exit(-1);
    }

    /* Duplicate input so we can safely pass a writable pointer. */
    gpu_input = strdup(input);

    CLCREATEARG(0, alg_buffer, CL_RO, hash_type, sizeof(hash_type));
    CLCREATEARG_ARRAY(1, input_buffer, CL_RO, gpu_input, strlen(gpu_input) + 1);
    CLCREATEARG(2, input_len_buffer, CL_RO, input_len, sizeof(gpu_uint));
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
        printf("\n\nGPU NetNTLMv1 Error:\n\tPlaintext:     %s\n"
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


int test_hash_netntlmv1(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(netntlmv1_test_inputs) /
                                            sizeof(netntlmv1_test_inputs[0]));

    for (i = 0; i < num_tests; i++) {
        const char *input = netntlmv1_test_inputs[i];
        unsigned char cpu_hash[8] = {0};
        char cpu_hex[17] = {0};

        /* Verify the DES key expansion contract: setup_des_key must produce
         * an 8-byte key that differs from the raw 7-byte plaintext (the
         * expansion spreads 56 bits across 64 bits with parity). */
        {
            unsigned char expanded[8] = {0};
            setup_des_key((char *)input, expanded);
            if (memcmp(expanded, input, 7) == 0) {
                fprintf(stderr, "NetNTLMv1 test %u: DES key expansion had no "
                        "effect - raw and expanded keys are identical\n", i);
                tests_passed = 0;
                continue;
            }
        }

        /* Compute CPU reference hash. */
        cpu_netntlmv1_hash(input, cpu_hash);
        if (!bytes_to_hex(cpu_hash, 8, cpu_hex, sizeof(cpu_hex))) {
            fprintf(stderr, "bytes_to_hex failed for NetNTLMv1 test %u\n", i);
            tests_passed = 0;
            continue;
        }

        /* Verify GPU matches CPU. */
        tests_passed &= gpu_test_netntlmv1_hash(device, context, kernel,
                                                 input, cpu_hex);
    }

    return tests_passed;
}
