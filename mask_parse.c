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
                           const MaskPosition *c1, const MaskPosition *c2,
                           const MaskPosition *c3, const MaskPosition *c4) {
    const char *src = NULL;
    const MaskPosition *cust = NULL;
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

    case '1': cust = c1; break;
    case '2': cust = c2; break;
    case '3': cust = c3; break;
    case '4': cust = c4; break;

    case 'h': src = "0123456789abcdef"; break;
    case 'H': src = "0123456789ABCDEF"; break;

    case '?':   /* ?? -> literal '?' */
        pos->chars[pos->size++] = '?';
        return 0;

    default:
        fprintf(stderr, "mask_parse: unknown specifier '?%c'\n", spec);
        return -1;
    }

    if (cust != NULL) {
        if (cust->size == 0) {
            fprintf(stderr, "mask_parse: empty custom charset\n");
            return -1;
        }
        if (cust->size > MAX_CHARSET_LEN) {
            fprintf(stderr, "mask_parse: custom charset size %u exceeds "
                    "MAX_CHARSET_LEN (%d)\n", cust->size, MAX_CHARSET_LEN);
            return -1;
        }
        memcpy(pos->chars, cust->chars, cust->size);
        pos->size = cust->size;
        return 0;
    }
    /* built-in specifier: src was set by the switch */
    while (*src && pos->size < MAX_CHARSET_LEN)
        pos->chars[pos->size++] = *src++;

    return 0;
}


static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

int expand_charset_def(const char *def, char out[MAX_CHARSET_LEN],
                       unsigned int *out_len) {
    unsigned int n = 0;
    size_t i = 0;

    if (def == NULL || out == NULL || out_len == NULL)
        return -1;

    while (def[i] != '\0') {
        if (def[i] == '?' && def[i + 1] != '\0') {
            char spec = def[i + 1];
            if (spec == '1' || spec == '2' || spec == '3' || spec == '4') {
                fprintf(stderr, "expand_charset_def: custom charset ?%c not "
                        "allowed inside a custom charset\n", spec);
                return -1;
            }
            if (spec == '?') {            /* ?? -> literal '?' */
                if (n >= MAX_CHARSET_LEN) goto overflow;
                out[n++] = '?';
                i += 2;
                continue;
            }
            /* Expand a built-in token via build_position into a temp position. */
            MaskPosition tmp;
            tmp.size = 0;
            if (build_position(spec, &tmp, NULL, NULL, NULL, NULL) != 0)
                return -1;
            if (n + tmp.size > MAX_CHARSET_LEN) goto overflow;
            memcpy(out + n, tmp.chars, tmp.size);
            n += tmp.size;
            i += 2;
        } else if (def[i] == '\\' && (def[i + 1] == 'x' || def[i + 1] == 'X') &&
                   hexval(def[i + 2]) >= 0 && hexval(def[i + 3]) >= 0) {
            if (n >= MAX_CHARSET_LEN) goto overflow;
            out[n++] = (char)((hexval(def[i + 2]) << 4) | hexval(def[i + 3]));
            i += 4;
        } else if (def[i] == '\\' && def[i + 1] == '\\') {
            if (n >= MAX_CHARSET_LEN) goto overflow;
            out[n++] = '\\';
            i += 2;
        } else {
            if (def[i] == '?') {   /* trailing '?' with no specifier */
                fprintf(stderr, "expand_charset_def: trailing '?' with no specifier\n");
                return -1;
            }
            if (n >= MAX_CHARSET_LEN) goto overflow;
            out[n++] = def[i++];
        }
    }

    *out_len = n;
    return 0;

overflow:
    fprintf(stderr, "expand_charset_def: definition exceeds %d bytes\n",
            MAX_CHARSET_LEN);
    return -1;
}


int mask_parse_ex(const char *mask_str, Mask *out,
                  const MaskPosition *c1, const MaskPosition *c2,
                  const MaskPosition *c3, const MaskPosition *c4) {
    int pos = 0;
    unsigned int i = 0;

    if (mask_str == NULL || out == NULL)
        return -1;

    memset(out, 0, sizeof(Mask));

    while (mask_str[i] != '\0') {
        if (pos >= MAX_PLAINTEXT_LEN) {
            fprintf(stderr, "mask_parse_ex: mask exceeds MAX_PLAINTEXT_LEN (%d)\n",
                    MAX_PLAINTEXT_LEN);
            return -1;
        }

        if (mask_str[i] == '?' && mask_str[i + 1] != '\0') {
            if (build_position(mask_str[i + 1], &out->positions[pos],
                               c1, c2, c3, c4) != 0)
                return -1;
            i += 2;
        } else {
            /* Trailing bare '?' with no specifier is an error. */
            if (mask_str[i] == '?') {
                fprintf(stderr, "mask_parse: trailing '?' with no specifier\n");
                return -1;
            }
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


int mask_parse(const char *mask_str, Mask *out,
               const char *c1, const char *c2,
               const char *c3, const char *c4) {
    MaskPosition p[4];
    const MaskPosition *pp[4] = { NULL, NULL, NULL, NULL };
    const char *raw[4] = { c1, c2, c3, c4 };
    int k;

    for (k = 0; k < 4; k++) {
        if (raw[k] != NULL) {
            if (expand_charset_def(raw[k], p[k].chars, &p[k].size) != 0)
                return -1;
            pp[k] = &p[k];
        }
    }
    return mask_parse_ex(mask_str, out, pp[0], pp[1], pp[2], pp[3]);
}


/* Returns the total keyspace of a mask (product of all position sizes).
 * Precondition: m->length > 0.  A zero-length mask returns 1 (not 0) because
 * mask_parse rejects empty masks before this function is called.  Callers that
 * construct a Mask struct directly must ensure m->length > 0.
 * Returns 0 on overflow. */
uint64_t mask_keyspace(const Mask *m) {
    uint64_t product = 1;
    int i;

    for (i = 0; i < m->length; i++) {
        if (m->positions[i].size != 0 && product > UINT64_MAX / m->positions[i].size) {
            fprintf(stderr, "mask_keyspace: overflow at position %d\n", i);
            return 0;
        }
        product *= m->positions[i].size;
    }

    return product;
}


void mask_encode_for_filename(const char *src, char *dst, size_t dst_len) {
    static const char valid[] = "ludsabhH1234";
    size_t j = 0;
    if (!src || !dst || dst_len == 0)
        return;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_len; i++) {
        /* Only encode '?' when followed by a valid specifier so that
         * mask_decode_from_filename is its exact inverse. */
        if (src[i] == '?' && src[i + 1] != '\0' && strchr(valid, src[i + 1])) {
            dst[j++] = '%';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}


void mask_decode_from_filename(char *s) {
    static const char valid[] = "ludsabhH1234";
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
