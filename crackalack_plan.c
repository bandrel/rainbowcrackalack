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
#include "shared.h"

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

/* Accurate coverage accounting for chain merges.
 *
 * Uses the iterative model where effective distinct chains after each
 * column are: m_{i+1} = N * (1 - exp(-m_i / N)).  This converges to
 * the fixed point m* = N * W(1) where W is the Lambert-W function
 * (≈ 0.5671 * N).  Once converged, remaining columns are computed in
 * O(1) via the constant contribution, making this fast even for very
 * long chains. */
static double coverage(uint64_t keyspace, uint64_t chain_len,
                       uint64_t num_chains) {
    if (keyspace == 0 || chain_len == 0 || num_chains == 0)
        return 0.0;

    double N = (double)keyspace;
    double m = (double)num_chains;
    double log_miss = 0.0;

    for (uint64_t i = 0; i < chain_len; i++) {
        double u = m / N;
        if (u >= 1.0)
            return 1.0;
        log_miss += log1p(-u);
        double m_new = N * -expm1(-m / N);
        if (fabs(m_new - m) < 1.0) {
            /* m has converged to its fixed point; remaining columns
             * each contribute the same miss probability. */
            uint64_t remaining = chain_len - i - 1;
            log_miss += (double)remaining * log1p(-m_new / N);
            break;
        }
        m = m_new;
    }

    return -expm1(log_miss);
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
                "<min_len> <max_len> <chain_len> <num_chains> "
                "[--markov-keyspace N]\n");
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
    char *end_cl, *end_nc;
    errno = 0;
    uint64_t chain_len = (uint64_t)strtoull(argv[4], &end_cl, 10);
    if (errno != 0 || end_cl == argv[4] || *end_cl != '\0') {
        fprintf(stderr, "Error: chain_len must be a valid integer, got '%s'.\n",
                argv[4]);
        return 1;
    }
    errno = 0;
    uint64_t num_chains = (uint64_t)strtoull(argv[5], &end_nc, 10);
    if (errno != 0 || end_nc == argv[5] || *end_nc != '\0') {
        fprintf(stderr, "Error: num_chains must be a valid integer, got '%s'.\n",
                argv[5]);
        return 1;
    }

    /* Optional flags. */
    uint64_t markov_keyspace = 0;  /* 0 = use full charset keyspace */
    for (int i = 6; i < argc; i++) {
        if (strcmp(argv[i], "--markov-keyspace") == 0 && i + 1 < argc) {
            char *endp;
            errno = 0;
            markov_keyspace = (uint64_t)strtoull(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' ||
                markov_keyspace == 0) {
                fprintf(stderr, "Error: --markov-keyspace must be a positive "
                        "integer, got '%s'.\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    uint64_t keyspace = 0;
    if (markov_keyspace > 0) {
        /* Markov-reduced keyspace: cover only the top-N most probable
         * plaintexts.  N is the keyspace for all coverage math. */
        keyspace = markov_keyspace;
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
    if (markov_keyspace > 0)
        printf("Keyspace:     %llu plaintexts (Markov-reduced top-N)\n",
               (unsigned long long)keyspace);
    else
        printf("Keyspace:     %llu plaintexts\n", (unsigned long long)keyspace);
    printf("Chains:       %llu\n", (unsigned long long)num_chains);
    printf("Chain length: %llu\n", (unsigned long long)chain_len);
    printf("File size:    %.2f MB\n", file_size_mb(num_chains));
    printf("Coverage:     ~%.1f%%  (single-table, merge-aware estimate)\n",
           cov * 100.0);
    return 0;
}

/* Binary search for num_chains that achieves target coverage using
 * the merge-aware model. */
static uint64_t solve_num_chains(uint64_t keyspace, uint64_t chain_len,
                                 double target) {
    /* Get a naive upper bound ignoring merges. */
    double p = (double)chain_len / (double)keyspace;
    if (p >= 1.0)
        return 1;
    uint64_t hi = (uint64_t)ceil(log(1.0 - target) / log(1.0 - p));
    /* Merges require more chains — start with 4x naive. */
    if (hi <= UINT64_MAX / 4)
        hi *= 4;
    else
        hi = keyspace;
    if (hi > keyspace)
        hi = keyspace;
    /* Ensure hi actually reaches the target. */
    while (coverage(keyspace, chain_len, hi) < target) {
        if (hi > keyspace / 2) { hi = keyspace; break; }
        hi *= 2;
    }

    uint64_t lo = 1;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (coverage(keyspace, chain_len, mid) < target)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int cmd_recommend(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "Usage: crackalack_plan recommend <hash> <charset_or_mask> "
                "<min_len> <max_len> <target_pct> [--tables N] "
                "[--chain-len L] [--markov-keyspace N] [--markov FILE]\n");
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

    /* Parse optional flags. */
    unsigned int num_tables = 1;
    uint64_t user_chain_len = 0;  /* 0 = auto */
    uint64_t markov_keyspace = 0; /* 0 = use full charset keyspace */
    const char *markov_path = NULL;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--markov-keyspace") == 0 && i + 1 < argc) {
            char *endp;
            errno = 0;
            markov_keyspace = (uint64_t)strtoull(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' ||
                markov_keyspace == 0) {
                fprintf(stderr, "Error: --markov-keyspace must be a positive "
                        "integer, got '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--markov") == 0 && i + 1 < argc) {
            markov_path = argv[++i];
        } else if (strcmp(argv[i], "--tables") == 0 && i + 1 < argc) {
            char *endp;
            errno = 0;
            unsigned long val = strtoul(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' ||
                val == 0 || val > 1000) {
                fprintf(stderr,
                        "Error: --tables must be in [1, 1000], got '%s'\n",
                        argv[i]);
                return 1;
            }
            num_tables = (unsigned int)val;
        } else if (strcmp(argv[i], "--chain-len") == 0 && i + 1 < argc) {
            char *endp;
            errno = 0;
            user_chain_len = (uint64_t)strtoull(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' ||
                user_chain_len == 0) {
                fprintf(stderr,
                        "Error: --chain-len must be a positive integer, "
                        "got '%s'\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

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

    if (markov_keyspace == 0 && markov_path != NULL) {
        fprintf(stderr, "Error: --markov requires --markov-keyspace to also "
                "be specified.\n");
        return 1;
    }

    uint64_t keyspace = 0;
    if (markov_keyspace > 0) {
        /* Markov-reduced keyspace: cover only the top-N most probable
         * plaintexts.  N is the keyspace for all coverage math. */
        keyspace = markov_keyspace;
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
    if (markov_keyspace > 0)
        printf("Keyspace:     %llu plaintexts (Markov-reduced top-N)\n",
               (unsigned long long)keyspace);
    else
        printf("Keyspace:     %llu plaintexts\n", (unsigned long long)keyspace);
    printf("Target:       %s combined coverage", target_str);
    if (num_tables > 1)
        printf(" across %u tables", num_tables);
    printf("\n");

    /* Trivial case. */
    if (keyspace == 1) {
        printf("\nRecommended:\n");
        printf("  Chain length: 1\n");
        printf("  Chains:       1\n");
        printf("  File size:    %.2f MB\n", file_size_mb(1));
        printf("  Coverage:     ~100.0%%\n");
        return 0;
    }

    /* Per-table coverage target.  Combined coverage of t independent
     * tables: C = 1 - (1 - c)^t  =>  c = 1 - (1 - C)^(1/t). */
    double per_table_target = 1.0 - pow(1.0 - target, 1.0 / num_tables);

    /* Chain length: user override or heuristic.
     * Single table: Hellman optimum chain_len ~ sqrt(N).
     * Multiple tables: shorter chains avoid excessive merges and keep
     * lookups fast.  Use N^(1/3) as a practical default. */
    uint64_t chain_len;
    if (user_chain_len > 0) {
        chain_len = user_chain_len;
    } else if (num_tables == 1) {
        chain_len = (uint64_t)(sqrt((double)keyspace) + 0.5);
    } else {
        chain_len = (uint64_t)(cbrt((double)keyspace) + 0.5);
    }
    if (chain_len < 1)
        chain_len = 1;
    if (chain_len > keyspace)
        chain_len = keyspace;

    uint64_t num_chains = solve_num_chains(keyspace, chain_len,
                                           per_table_target);

    double per_table_cov = coverage(keyspace, chain_len, num_chains);
    double combined_cov = 1.0 - pow(1.0 - per_table_cov, num_tables);

    printf("\nRecommended (per table index):\n");
    printf("  Chain length:     %llu\n", (unsigned long long)chain_len);
    printf("  Chains per table: %llu\n", (unsigned long long)num_chains);
    printf("  File size/table:  %.2f MB\n", file_size_mb(num_chains));
    printf("  Coverage/table:   ~%.1f%%\n", per_table_cov * 100.0);
    if (num_tables > 1) {
        printf("\nTotals (%u table indices):\n", num_tables);
        printf("  Total chains:     %llu\n",
               (unsigned long long)num_chains * num_tables);
        printf("  Total files:      %u\n", num_tables);
        printf("  Total file size:  %.2f MB\n",
               file_size_mb(num_chains) * num_tables);
    }
    printf("  Combined coverage: ~%.1f%%\n", combined_cov * 100.0);

    /* Print generation commands. */
    printf("\nGeneration commands:\n");
    for (unsigned int t = 0; t < num_tables; t++) {
        printf("  ./crackalack_gen %s %s %u %u %u %llu %llu 0",
               hash_name, charset_or_mask, min_len, max_len,
               t, (unsigned long long)chain_len,
               (unsigned long long)num_chains);
        if (markov_keyspace > 0) {
            printf(" --markov %s --markov-keyspace %llu",
                   markov_path ? markov_path : "<model.markov>",
                   (unsigned long long)markov_keyspace);
        }
        printf("\n");
    }
    if (markov_keyspace > 0 && markov_path == NULL)
        printf("\nNote: pass --markov <model.markov> to embed your trained "
               "model path in the commands above.\n");
    return 0;
}

static int cmd_train(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: crackalack_plan train <wordlist> [charset] [--max-positions N]\n");
        fprintf(stderr, "  Default charset: ascii-32-95\n");
        fprintf(stderr, "  Default max-positions: %d (position-aware bigram tables)\n",
                MARKOV_DEFAULT_MAX_POSITIONS);
        return 1;
    }

    const char *wordlist_path = argv[0];
    const char *charset_name = "ascii-32-95";
    unsigned int max_positions = 0;  /* 0 = use default */

    /* Parse remaining arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--max-positions") == 0 && i + 1 < argc) {
            char *endp;
            errno = 0;
            unsigned long val = strtoul(argv[++i], &endp, 10);
            if (errno != 0 || endp == argv[i] || *endp != '\0' || val == 0 || val > MAX_PLAINTEXT_LEN) {
                fprintf(stderr, "Error: --max-positions must be in [1, %d], got '%s'\n",
                        MAX_PLAINTEXT_LEN, argv[i]);
                return 1;
            }
            max_positions = (unsigned int)val;
        } else if (argv[i][0] != '-') {
            charset_name = argv[i];
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

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

    char *charset_string = validate_charset((char *)charset_name);
    if (charset_string == NULL) {
        fprintf(stderr, "Error: unknown charset '%s'.\n", charset_name);
        return 1;
    }

    unsigned int charset_len = (unsigned int)strlen(charset_string);

    markov_model model = {0};
    if (markov_train(wordlist_path, charset_string, charset_len, max_positions, &model) != 0) {
        markov_free(&model);
        return 1;
    }

    if (markov_save(output_path, &model) != 0) {
        markov_free(&model);
        return 1;
    }

    unsigned int saved_max_positions = model.max_positions;
    markov_free(&model);
    printf("Trained position-aware model (max_positions=%u) written to '%s'\n",
           saved_max_positions, output_path);
    return 0;
}

static void print_usage(void) {
    fprintf(stderr,
            "Usage: crackalack_plan <subcommand> ...\n"
            "\n"
            "Subcommands:\n"
            "  estimate <hash> <charset_or_mask> <min_len> <max_len> "
            "<chain_len> <num_chains> [--markov-keyspace N]\n"
            "  recommend <hash> <charset_or_mask> <min_len> <max_len> "
            "<target_pct> [--tables N] [--chain-len L] "
            "[--markov-keyspace N] [--markov FILE]\n"
            "  train <wordlist> [charset] [--max-positions N]\n"
            "\n"
            "The train command creates position-aware Markov models (v1).\n"
            "Use --max-positions to control the number of position tables.\n"
            "\n"
            "Pass --markov-keyspace N to estimate/recommend coverage against a\n"
            "Markov-reduced keyspace (the top-N most probable plaintexts)\n"
            "instead of the full charset keyspace.\n");
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
