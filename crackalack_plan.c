/*
 * Rainbow Crackalack: crackalack_plan.c
 * Copyright (C) 2018-2021  Joe Testa <jtesta@positronsecurity.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms version 3 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charset.h"
#include "markov.h"
#include "mask_parse.h"

#define MAX_PLAINTEXT_LEN 16

/* Compute keyspace for a named charset across [min_len, max_len]. */
static int charset_keyspace(const char *name, unsigned int min_len,
                            unsigned int max_len, uint64_t *out_keyspace,
                            uint64_t *out_charset_len) {
    char *chars = validate_charset((char *)name);
    if (chars == NULL) {
        fprintf(stderr, "Error: unknown charset '%s'.\n", name);
        return -1;
    }

    uint64_t clen = (uint64_t)strlen(chars);
    *out_charset_len = clen;

    uint64_t keyspace = 0;
    for (unsigned int l = min_len; l <= max_len; l++) {
        uint64_t power = 1;
        for (unsigned int i = 0; i < l; i++) {
            if (power > UINT64_MAX / clen) {
                fprintf(stderr,
                        "Error: keyspace overflows uint64_t at length %u.\n",
                        l);
                return -1;
            }
            power *= clen;
        }
        if (keyspace > UINT64_MAX - power) {
            fprintf(stderr, "Error: total keyspace overflows uint64_t.\n");
            return -1;
        }
        keyspace += power;
    }
    *out_keyspace = keyspace;
    return 0;
}

/* Compute keyspace for a mask string across [min_len, max_len].
 * For a fixed-length mask, only min_len == max_len makes sense; if they
 * differ we sum keyspace over each length where the mask length matches. */
static int mask_keyspace_range(const char *mask_str, unsigned int min_len,
                               unsigned int max_len, uint64_t *out_keyspace) {
    Mask m;
    if (mask_parse(mask_str, &m, NULL, NULL, NULL, NULL) != 0)
        return -1;

    unsigned int mask_len = (unsigned int)m.length;

    /* Validate that the mask length falls within [min_len, max_len]. */
    if (mask_len < min_len || mask_len > max_len) {
        fprintf(stderr,
                "Warning: mask length %u is outside [%u, %u]; "
                "treating keyspace as 0.\n",
                mask_len, min_len, max_len);
        *out_keyspace = 0;
        return 0;
    }

    *out_keyspace = mask_keyspace(&m);
    return 0;
}

static double coverage(uint64_t keyspace, uint64_t chain_len,
                       uint64_t num_chains) {
    if (keyspace == 0)
        return 0.0;
    uint64_t cl = chain_len < keyspace ? chain_len : keyspace;
    return 1.0 - pow(1.0 - (double)cl / (double)keyspace, (double)num_chains);
}

static double file_size_mb(uint64_t num_chains) {
    return (double)num_chains * 16.0 / (1024.0 * 1024.0);
}

/* Parse a plaintext length argument in [1, MAX_PLAINTEXT_LEN].
 * Returns -1 on error (message already printed). */
static int parse_len(const char *s, const char *name, unsigned int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        fprintf(stderr, "Error: %s must be a valid integer, got '%s'.\n",
                name, s);
        return -1;
    }
    if (v < 1 || v > MAX_PLAINTEXT_LEN) {
        fprintf(stderr,
                "Error: %s must be in [1, %d], got %ld.\n",
                name, MAX_PLAINTEXT_LEN, v);
        return -1;
    }
    *out = (unsigned int)v;
    return 0;
}

static int cmd_estimate(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr,
                "Usage: crackalack_plan estimate <hash> <charset_or_mask> "
                "<min_len> <max_len> <chain_len> <num_chains>\n");
        return 1;
    }

    /* argv[0] = hash (informational only), argv[1] = charset/mask,
     * argv[2] = min_len, argv[3] = max_len,
     * argv[4] = chain_len, argv[5] = num_chains */
    const char *hash_name      = argv[0];
    const char *charset_or_mask = argv[1];
    unsigned int min_len, max_len;
    if (parse_len(argv[2], "min_len", &min_len) != 0)
        return 1;
    if (parse_len(argv[3], "max_len", &max_len) != 0)
        return 1;
    uint64_t chain_len         = (uint64_t)strtoull(argv[4], NULL, 10);
    uint64_t num_chains        = (uint64_t)strtoull(argv[5], NULL, 10);

    uint64_t keyspace = 0;

    if (is_mask_string(charset_or_mask)) {
        if (mask_keyspace_range(charset_or_mask, min_len, max_len,
                                &keyspace) != 0)
            return 1;
    } else {
        uint64_t dummy_clen;
        if (charset_keyspace(charset_or_mask, min_len, max_len,
                             &keyspace, &dummy_clen) != 0)
            return 1;
    }

    if (keyspace == 0) {
        fprintf(stderr, "Error: computed keyspace is 0.\n");
        return 1;
    }

    double cov = coverage(keyspace, chain_len, num_chains);

    printf("=== Table estimate ===\n");
    printf("Hash:         %s\n", hash_name);
    printf("Mask/Charset: %s\n", charset_or_mask);
    printf("Keyspace:     %llu plaintexts\n", (unsigned long long)keyspace);
    printf("Chains:       %llu\n", (unsigned long long)num_chains);
    printf("Chain length: %llu\n", (unsigned long long)chain_len);
    printf("File size:    %.2f MB\n", file_size_mb(num_chains));
    printf("Coverage:     ~%.1f%%  (approximate single-table estimate)\n",
           cov * 100.0);
    return 0;
}

static int cmd_recommend(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "Usage: crackalack_plan recommend <hash> <charset_or_mask> "
                "<min_len> <max_len> <target_pct>\n");
        return 1;
    }

    const char *hash_name       = argv[0];
    const char *charset_or_mask = argv[1];
    unsigned int min_len, max_len;
    if (parse_len(argv[2], "min_len", &min_len) != 0)
        return 1;
    if (parse_len(argv[3], "max_len", &max_len) != 0)
        return 1;
    const char *target_str      = argv[4];

    /* Parse target percentage - strip trailing '%' if present. */
    char target_buf[64];
    strncpy(target_buf, target_str, sizeof(target_buf) - 1);
    target_buf[sizeof(target_buf) - 1] = '\0';
    size_t tlen = strlen(target_buf);
    if (tlen > 0 && target_buf[tlen - 1] == '%')
        target_buf[tlen - 1] = '\0';
    char *end;
    errno = 0;
    double target_pct = strtod(target_buf, &end);
    if (errno != 0 || end == target_buf || *end != '\0') {
        fprintf(stderr,
                "Error: target coverage must be a number, got '%s'.\n",
                target_str);
        return 1;
    }
    double target = target_pct / 100.0;

    if (target <= 0.0 || target >= 1.0) {
        fprintf(stderr,
                "Error: target coverage must be between 0%% and 100%% "
                "(exclusive). Got: %s\n", target_str);
        return 1;
    }

    uint64_t keyspace = 0;

    if (is_mask_string(charset_or_mask)) {
        if (mask_keyspace_range(charset_or_mask, min_len, max_len,
                                &keyspace) != 0)
            return 1;
    } else {
        uint64_t dummy_clen;
        if (charset_keyspace(charset_or_mask, min_len, max_len,
                             &keyspace, &dummy_clen) != 0)
            return 1;
    }

    if (keyspace == 0) {
        fprintf(stderr, "Error: computed keyspace is 0.\n");
        return 1;
    }

    printf("=== Parameter recommendation ===\n");
    printf("Hash:         %s\n", hash_name);
    printf("Mask/Charset: %s\n", charset_or_mask);
    printf("Keyspace:     %llu plaintexts\n", (unsigned long long)keyspace);
    printf("Target:       %s coverage\n", target_str);
    printf("\nRecommended:\n");

    /* Trivial case. */
    if (keyspace == 1) {
        printf("  Chain length: 1\n");
        printf("  Chains:       1\n");
        printf("  File size:    %.2f MB\n", file_size_mb(1));
        printf("  Coverage:     ~100.0%%\n");
        return 0;
    }

    /* Hellman optimum: chain_len ~ sqrt(keyspace). */
    uint64_t chain_len = (uint64_t)(sqrt((double)keyspace) + 0.5);
    if (chain_len < 1)
        chain_len = 1;
    if (chain_len > keyspace)
        chain_len = keyspace;

    /* Solve for num_chains: coverage = 1 - (1 - cl/N)^n
     * => n = ceil(log(1 - target) / log(1 - cl/N)) */
    double p_miss_per_chain = 1.0 - (double)chain_len / (double)keyspace;
    uint64_t num_chains;
    if (p_miss_per_chain <= 0.0) {
        /* A single chain covers everything. */
        num_chains = 1;
    } else {
        double n = ceil(log(1.0 - target) / log(p_miss_per_chain));
        if (n < 1.0)
            n = 1.0;
        num_chains = (uint64_t)n;
    }

    double actual_cov = coverage(keyspace, chain_len, num_chains);

    printf("  Chain length: %llu\n", (unsigned long long)chain_len);
    printf("  Chains:       %llu\n", (unsigned long long)num_chains);
    printf("  File size:    %.2f MB\n", file_size_mb(num_chains));
    printf("  Coverage:     ~%.1f%%\n", actual_cov * 100.0);
    return 0;
}

static int cmd_train(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: crackalack_plan train <wordlist>\n");
        return 1;
    }

    const char *wordlist_path = argv[0];

    const char *base = strrchr(wordlist_path, '/');
    base = base ? base + 1 : wordlist_path;

    char output_path[256];
    const char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        snprintf(output_path, sizeof(output_path), "%.*s.markov",
                 (int)(dot - base), base);
    } else {
        snprintf(output_path, sizeof(output_path), "%s.markov", base);
    }

    char *charset_string = validate_charset("ascii-32-95");
    if (charset_string == NULL) {
        fprintf(stderr, "Error: built-in charset 'ascii-32-95' not found.\n");
        return 1;
    }

    unsigned int charset_len = (unsigned int)strlen(charset_string);

    markov_model model = {0};
    if (markov_train(wordlist_path, charset_string, charset_len, &model) != 0) {
        markov_free(&model);
        return 1;
    }

    if (markov_save(output_path, &model) != 0) {
        markov_free(&model);
        return 1;
    }

    markov_free(&model);
    printf("Trained model written to '%s'\n", output_path);
    return 0;
}

static void print_usage(void) {
    fprintf(stderr,
            "Usage: crackalack_plan <subcommand> ...\n"
            "\n"
            "Subcommands:\n"
            "  estimate <hash> <charset_or_mask> <min_len> <max_len> "
            "<chain_len> <num_chains>\n"
            "  recommend <hash> <charset_or_mask> <min_len> <max_len> "
            "<target_pct>\n"
            "  train <wordlist>\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "estimate") == 0)
        return cmd_estimate(argc - 2, argv + 2);

    if (strcmp(subcmd, "recommend") == 0)
        return cmd_recommend(argc - 2, argv + 2);

    if (strcmp(subcmd, "train") == 0)
        return cmd_train(argc - 2, argv + 2);

    fprintf(stderr, "Error: unknown subcommand '%s'.\n\n", subcmd);
    print_usage();
    return 1;
}
