#include "mask_parse.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

/* Hashcat built-in charsets */
static const char CHARSET_L[] = "abcdefghijklmnopqrstuvwxyz";
static const char CHARSET_U[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char CHARSET_D[] = "0123456789";
/* ?s = all printable non-alphanumeric ASCII (space through ~) */
static const char CHARSET_S[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";


int is_mask_string(const char *s) {
    if (s == NULL)
        return 0;
    return (strchr(s, '?') != NULL);
}


static int build_position(char spec, MaskPosition *pos,
                           const char *c1, const char *c2,
                           const char *c3, const char *c4) {
    const char *src = NULL;
    unsigned int i;

    pos->size = 0;

    switch (spec) {
    case 'l': src = CHARSET_L; break;
    case 'u': src = CHARSET_U; break;
    case 'd': src = CHARSET_D; break;
    case 's': src = CHARSET_S; break;

    case 'a': {
        /* ?a = ?l + ?u + ?d + ?s */
        const char *parts[4] = { CHARSET_L, CHARSET_U, CHARSET_D, CHARSET_S };
        for (int p = 0; p < 4; p++) {
            const char *ch = parts[p];
            while (*ch && pos->size < MAX_CHARSET_LEN)
                pos->chars[pos->size++] = *ch++;
        }
        return 0;
    }

    case 'b':
        /* ?b = all 256 byte values */
        for (i = 0; i < 256 && pos->size < MAX_CHARSET_LEN; i++)
            pos->chars[pos->size++] = (char)(unsigned char)i;
        return 0;

    case '1': src = c1; break;
    case '2': src = c2; break;
    case '3': src = c3; break;
    case '4': src = c4; break;

    default:
        fprintf(stderr, "mask_parse: unknown specifier '?%c'\n", spec);
        return -1;
    }

    if (src == NULL) {
        fprintf(stderr, "mask_parse: custom charset ?%c was not provided\n", spec);
        return -1;
    }

    while (*src && pos->size < MAX_CHARSET_LEN)
        pos->chars[pos->size++] = *src++;

    return 0;
}


int mask_parse(const char *mask_str, Mask *out,
               const char *c1, const char *c2,
               const char *c3, const char *c4) {
    int pos = 0;
    unsigned int i = 0;

    if (mask_str == NULL || out == NULL)
        return -1;

    memset(out, 0, sizeof(Mask));

    while (mask_str[i] != '\0') {
        if (pos >= MAX_PLAINTEXT_LEN) {
            fprintf(stderr, "mask_parse: mask exceeds MAX_PLAINTEXT_LEN (%d)\n",
                    MAX_PLAINTEXT_LEN);
            return -1;
        }

        if (mask_str[i] == '?' && mask_str[i + 1] != '\0') {
            if (build_position(mask_str[i + 1], &out->positions[pos],
                               c1, c2, c3, c4) != 0)
                return -1;
            i += 2;
        } else {
            /* Literal character - single-char position */
            out->positions[pos].chars[0] = mask_str[i];
            out->positions[pos].size = 1;
            i++;
        }

        if (out->positions[pos].size == 0) {
            fprintf(stderr, "mask_parse: empty charset at position %d\n", pos);
            return -1;
        }
        pos++;
    }

    out->length = pos;
    return 0;
}


uint64_t mask_keyspace(const Mask *m) {
    uint64_t product = 1;
    int i;

    for (i = 0; i < m->length; i++)
        product *= m->positions[i].size;

    return product;
}


void mask_encode_for_filename(const char *src, char *dst, size_t dst_len) {
    size_t j = 0;
    if (!src || !dst || dst_len == 0)
        return;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_len; i++) {
        if (src[i] == '?') {
            dst[j++] = '%';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}


void mask_decode_from_filename(char *s) {
    static const char valid[] = "ludsab1234";
    char *r = s, *w = s;
    if (!s)
        return;
    while (*r) {
        if (*r == '%' && *(r + 1) != '\0' && strchr(valid, *(r + 1))) {
            *w++ = '?';
            r++;
            *w++ = *r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}


void mask_to_gpu_buffers(const Mask *m,
                         char         mask_data[MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN],
                         unsigned int mask_lens[MAX_PLAINTEXT_LEN]) {
    int i;

    memset(mask_data, 0, MAX_PLAINTEXT_LEN * MAX_CHARSET_LEN);
    memset(mask_lens, 0, MAX_PLAINTEXT_LEN * sizeof(unsigned int));

    for (i = 0; i < m->length; i++) {
        mask_lens[i] = m->positions[i].size;
        memcpy(mask_data + i * MAX_CHARSET_LEN,
               m->positions[i].chars,
               m->positions[i].size);
    }
}
