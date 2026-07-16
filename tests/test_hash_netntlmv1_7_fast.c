/*
 * Rainbow Crackalack: test_hash_netntlmv1_7_fast.c
 * GPU test for the OPTIMIZED NetNTLMv1-7 fast path (hash_netntlmv1_7_fast_ip):
 * the challenge-IP hoist + on-the-fly DES key schedule.
 *
 * The generic NetNTLMv1 chain/hash tests exercise the table-DES netntlmv1_hash;
 * they do NOT touch the fast path used by the precompute / false-alarm /
 * generation kernels.  This test invokes hash_netntlmv1_7_fast_ip directly
 * (one plaintext per dispatch) and compares its 8-byte output against the CPU
 * reference (setup_des_key + netntlmv1_hash) under a fixed challenge.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpu_backend.h"

#include "cpu_rt_functions.h"
#include "misc.h"
#include "shared.h"
#include "test_shared.h"
#include "test_hash_netntlmv1_7_fast.h"


/* 7-byte plaintexts (DES keys) covering low/high bytes and printable ranges. */
static const char *fast_test_inputs[] = {
    "abcdefg",
    "1234567",
    "ABCDEFG",
    "passwd1",
    " !#$%&(",
    "\x01\x02\x03\x04\x05\x06\x07",
    "\xfe\xfd\xfc\xfb\xfa\xf9\xf8",
    "\x00\x10\x20\x30\x40\x50\x60",
};

/* Non-trivial fixed challenge (exercises the IP hoist with real bits). */
static const unsigned char fast_test_challenge[8] =
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };


static int gpu_test_one(gpu_device device, gpu_context context, gpu_kernel kernel,
                        const unsigned char *input7,
                        const unsigned char *cpu_hash)
{
    CLMAKETESTVARS();
    int test_passed = 0;

    gpu_buffer plaintext_buf = NULL, challenge_buf = NULL, output_buf = NULL;

    unsigned char gpu_plaintext[8] = {0};
    unsigned char gpu_challenge[8] = {0};
    unsigned char *output = calloc(8, sizeof(unsigned char));
    if (!output) {
        fprintf(stderr, "Error allocating output in test_hash_netntlmv1_7_fast\n");
        exit(-1);
    }
    memcpy(gpu_plaintext, input7, 7);
    memcpy(gpu_challenge, fast_test_challenge, 8);

    queue = CLCREATEQUEUE(context, device);

    CLCREATEARG_ARRAY(0, plaintext_buf, CL_RO, gpu_plaintext, 8);
    CLCREATEARG_ARRAY(1, challenge_buf, CL_RO, gpu_challenge, 8);
    CLCREATEARG_ARRAY(2, output_buf, CL_WO, output, 8);

    CLRUNKERNEL(queue, kernel, &global_work_size);
    CLFLUSH(queue);
    CLWAIT(queue);

    CLREADBUFFER(output_buf, 8, output);

    if (memcmp(output, cpu_hash, 8) == 0) {
        test_passed = 1;
    } else {
        int i;
        printf("\n\nGPU NetNTLMv1-7 fast-path error:\n\tPlaintext (hex): ");
        for (i = 0; i < 7; i++) printf("%02x", input7[i]);
        printf("\n\tExpected hash:   ");
        for (i = 0; i < 8; i++) printf("%02x", cpu_hash[i]);
        printf("\n\tComputed hash:   ");
        for (i = 0; i < 8; i++) printf("%02x", output[i]);
        printf("\n\n");
    }

    CLFREEBUFFER(plaintext_buf);
    CLFREEBUFFER(challenge_buf);
    CLFREEBUFFER(output_buf);
    CLRELEASEQUEUE(queue);

    FREE(output);
    return test_passed;
}


int test_hash_netntlmv1_7_fast(gpu_device device, gpu_context context, gpu_kernel kernel)
{
    int tests_passed = 1;
    unsigned int i;
    unsigned int num_tests = (unsigned int)(sizeof(fast_test_inputs) /
                                            sizeof(fast_test_inputs[0]));

    /* CPU reference must use the same challenge the GPU kernel is given. */
    set_netntlmv1_challenge(fast_test_challenge);

    for (i = 0; i < num_tests; i++) {
        const unsigned char *input = (const unsigned char *)fast_test_inputs[i];
        unsigned char key[8] = {0};
        unsigned char cpu_hash[8] = {0};

        setup_des_key((char *)input, key);
        netntlmv1_hash(key, 8, cpu_hash);

        tests_passed &= gpu_test_one(device, context, kernel, input, cpu_hash);
    }

    return tests_passed;
}
