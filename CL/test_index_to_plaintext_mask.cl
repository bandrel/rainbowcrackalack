#include "rt.cl"
#include "shared.h"
#include "string.cl"

/*
 * GPU test kernel for index_to_plaintext with mask mode (is_mask=1).
 * Identical in structure to test_index_to_plaintext.cl but registered
 * under a distinct name for the mask GPU test suite.
 */
__kernel void test_index_to_plaintext_mask(
    __global char *g_charset,
    __global unsigned int *g_charset_len,
    __global unsigned int *g_plaintext_len_min,
    __global unsigned int *g_plaintext_len_max,
    __global unsigned long *g_index,
    __global unsigned char *g_plaintext,
    __global unsigned int *g_plaintext_len,
    __global unsigned char *g_debug,
    __global unsigned int *g_is_mask,
    __global char *g_mask_charset_data,
    __global unsigned int *g_mask_charset_lens)
{
    unsigned long plaintext_space_up_to_index[MAX_PLAINTEXT_LEN];

    char charset[MAX_CHARSET_LEN];
    unsigned int plaintext_len_min = *g_plaintext_len_min;
    unsigned int plaintext_len_max = *g_plaintext_len_max;
    unsigned long index = *g_index;
    unsigned char plaintext[MAX_PLAINTEXT_LEN];
    unsigned int plaintext_len = *g_plaintext_len;
    unsigned int is_mask = *g_is_mask;

    unsigned int charset_len = *g_charset_len;
    g_memcpy((unsigned char *)charset, (unsigned char __global *)g_charset, charset_len);

    fill_plaintext_space_table(charset_len, plaintext_len_min, plaintext_len_max,
                                plaintext_space_up_to_index);

    index_to_plaintext(index, charset, charset_len, is_mask,
                        g_mask_charset_data, g_mask_charset_lens,
                        plaintext_len_min, plaintext_len_max,
                        plaintext_space_up_to_index, plaintext, &plaintext_len);

    *g_plaintext_len = plaintext_len;
    for (int i = 0; i < plaintext_len; i++)
        g_plaintext[i] = plaintext[i];
}
