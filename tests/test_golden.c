/*
 * Rainbow Crackalack: test_golden.c
 *
 * Golden-vector tests for the CPU-side rainbow-table primitives
 * (ntlm_hash, hash_to_index, index_to_plaintext).
 *
 * Cross-backend coverage chain
 * ============================
 * These goldens pin the CPU reference implementations in cpu_rt_functions.c.
 * The per-backend GPU kernel tests (test_hash, test_chain, test_hash_to_index,
 * test_index_to_plaintext, etc.) each validate their backend's kernel output
 * against those same CPU functions.  Therefore a backend (CUDA / Metal / OpenCL)
 * that produces different hash/reduce/decode output than the golden will fail
 * either here (CPU path) or in its own GPU kernel test (backend-vs-CPU diff).
 * This gives transitive cross-backend parity coverage with no GPU required here.
 *
 * Vector classification
 * =====================
 * INDEPENDENTLY VERIFIED  — value matches a published external reference.
 * PINNED FROM IMPL        — value computed from the current implementation
 *                           (2026-06-30); change only with intent.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "charset.h"
#include "cpu_rt_functions.h"
#include "shared.h"
#include "test_shared.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int check_hash(const char *label,
                      unsigned char *got, unsigned int got_len,
                      const char *want_hex)
{
    char got_hex[65] = {0};
    bytes_to_hex(got, got_len, got_hex, sizeof(got_hex));
    if (strcmp(got_hex, want_hex) != 0) {
        fprintf(stderr, "FAIL golden %s: got %s want %s\n", label, got_hex, want_hex);
        return 0;
    }
    return 1;
}

static int check_u64(const char *label, uint64_t got, uint64_t want)
{
    if (got != want) {
        fprintf(stderr, "FAIL golden %s: got %"PRIu64" want %"PRIu64"\n",
                label, got, want);
        return 0;
    }
    return 1;
}

static int check_plaintext_hex(const char *label,
                                char *got, unsigned int got_len,
                                const char *want_hex, unsigned int want_len)
{
    char got_hex[65] = {0};
    if (got_len != want_len) {
        fprintf(stderr, "FAIL golden %s: got len %u want %u\n",
                label, got_len, want_len);
        return 0;
    }
    bytes_to_hex((unsigned char *)got, got_len, got_hex, sizeof(got_hex));
    if (strcmp(got_hex, want_hex) != 0) {
        fprintf(stderr, "FAIL golden %s: got %s want %s\n", label, got_hex, want_hex);
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* NTLM hash goldens                                                    */
/* ------------------------------------------------------------------ */

static int test_ntlm(void)
{
    int ok = 1;
    unsigned char hash[16];

    /* INDEPENDENTLY VERIFIED — canonical NTLM hash of "password".
     * Well-known reference: https://en.wikipedia.org/wiki/NT_LAN_Manager
     * and every major password-cracking reference. */
    memset(hash, 0, sizeof(hash));
    ntlm_hash("password", 8, hash);
    ok &= check_hash("ntlm(\"password\")", hash, 16,
                     "8846f7eaee8fb117ad06bdd830b7586c");

    /* INDEPENDENTLY VERIFIED — canonical NTLM hash of empty string.
     * Value: MD4(UTF-16LE("")) = 31d6cfe0d16ae931b73c59d7e0c089c0.
     * Published in numerous NTLM cracking and penetration testing resources. */
    memset(hash, 0, sizeof(hash));
    ntlm_hash("", 0, hash);
    ok &= check_hash("ntlm(\"\")", hash, 16,
                     "31d6cfe0d16ae931b73c59d7e0c089c0");

    /* PINNED FROM IMPL (2026-06-30) — ntlm_hash("abc").
     * Value: MD4(UTF-16LE("abc")) = e0fba38268d0ec66ef1cb452d5885e53.
     * Change only with intent. */
    memset(hash, 0, sizeof(hash));
    ntlm_hash("abc", 3, hash);
    ok &= check_hash("ntlm(\"abc\")", hash, 16,
                     "e0fba38268d0ec66ef1cb452d5885e53");

    /* PINNED FROM IMPL (2026-06-30) — ntlm_hash("Password1!").
     * A mixed-case + digit + symbol password representative of real-world use.
     * Change only with intent. */
    memset(hash, 0, sizeof(hash));
    ntlm_hash("Password1!", 10, hash);
    ok &= check_hash("ntlm(\"Password1!\")", hash, 16,
                     "7facdc498ed1680c4fd1448319a8c04f");

    return ok;
}

/* ------------------------------------------------------------------ */
/* hash_to_index goldens (ascii-32-95, 8-char and 9-char keyspaces)    */
/* ------------------------------------------------------------------ */

static int test_hash_to_index(void)
{
    int ok = 1;
    uint64_t pspace_up_to8[MAX_PLAINTEXT_LEN + 1] = {0};
    uint64_t pspace_up_to9[MAX_PLAINTEXT_LEN + 1] = {0};
    unsigned char h_password[16];

    ntlm_hash("password", 8, h_password);

    uint64_t pspace8 = fill_plaintext_space_table(
        CHARSET_ASCII_32_95_LEN, 8, 8, pspace_up_to8);
    uint64_t pspace9 = fill_plaintext_space_table(
        CHARSET_ASCII_32_95_LEN, 9, 9, pspace_up_to9);

    /* PINNED FROM IMPL (2026-06-30).
     * Input: NTLM("password") = 8846f7eaee8fb117ad06bdd830b7586c.
     * Keyspace: ascii-32-95 8-char (95^8 = 6634204312890625).
     * Change only with intent. */
    ok &= check_u64("h2i(ntlm(pwd), 8-char, off=0, pos=0)",
                    hash_to_index(h_password, 16, 0, pspace8, 0),
                    UINT64_C(2313481644300423));

    ok &= check_u64("h2i(ntlm(pwd), 8-char, off=0, pos=1000)",
                    hash_to_index(h_password, 16, 0, pspace8, 1000),
                    UINT64_C(2313481644301423));

    ok &= check_u64("h2i(ntlm(pwd), 8-char, off=65536, pos=500)",
                    hash_to_index(h_password, 16, 65536, pspace8, 500),
                    UINT64_C(2313481644366459));

    /* PINNED FROM IMPL (2026-06-30).
     * Keyspace: ascii-32-95 9-char (95^9 = 630249409724609375).
     * Change only with intent. */
    ok &= check_u64("h2i(ntlm(pwd), 9-char, off=0, pos=0)",
                    hash_to_index(h_password, 16, 0, pspace9, 0),
                    UINT64_C(446805170607972298));

    ok &= check_u64("h2i(ntlm(pwd), 9-char, off=0, pos=5000)",
                    hash_to_index(h_password, 16, 0, pspace9, 5000),
                    UINT64_C(446805170607977298));

    return ok;
}

/* ------------------------------------------------------------------ */
/* index_to_plaintext goldens (ascii-32-95, 8-char and 9-char)         */
/* ------------------------------------------------------------------ */

static int test_index_to_plaintext(void)
{
    int ok = 1;
    uint64_t pspace_up_to8[MAX_PLAINTEXT_LEN + 1] = {0};
    uint64_t pspace_up_to9[MAX_PLAINTEXT_LEN + 1] = {0};
    char plaintext[MAX_PLAINTEXT_LEN + 2] = {0};
    unsigned int plaintext_len = 0;
    char *charset = CHARSET_ASCII_32_95;
    unsigned int charset_len = CHARSET_ASCII_32_95_LEN;

    fill_plaintext_space_table(charset_len, 8, 8, pspace_up_to8);
    fill_plaintext_space_table(charset_len, 9, 9, pspace_up_to9);

    /* PINNED FROM IMPL (2026-06-30) — index 0 in ascii-32-95 8-char keyspace.
     * First char of ascii-32-95 is space (0x20); index 0 = 8 spaces.
     * Change only with intent. */
    memset(plaintext, 0, sizeof(plaintext));
    plaintext_len = 0;
    index_to_plaintext(0, charset, charset_len, 8, 8, pspace_up_to8,
                       plaintext, &plaintext_len);
    ok &= check_plaintext_hex("i2p(0, ascii8)", plaintext, plaintext_len,
                               "2020202020202020", 8);

    /* PINNED FROM IMPL (2026-06-30) — index 1 in ascii-32-95 8-char keyspace.
     * Second char of ascii-32-95 is '!' (0x21); index 1 = "       !".
     * Change only with intent. */
    memset(plaintext, 0, sizeof(plaintext));
    plaintext_len = 0;
    index_to_plaintext(1, charset, charset_len, 8, 8, pspace_up_to8,
                       plaintext, &plaintext_len);
    ok &= check_plaintext_hex("i2p(1, ascii8)", plaintext, plaintext_len,
                               "2020202020202021", 8);

    /* PINNED FROM IMPL (2026-06-30) — index 12345678 in ascii-32-95 8-char keyspace.
     * Change only with intent. */
    memset(plaintext, 0, sizeof(plaintext));
    plaintext_len = 0;
    index_to_plaintext(12345678, charset, charset_len, 8, 8, pspace_up_to8,
                       plaintext, &plaintext_len);
    ok &= check_plaintext_hex("i2p(12345678, ascii8)", plaintext, plaintext_len,
                               "202020202e457950", 8);

    /* PINNED FROM IMPL (2026-06-30) — index 0 in ascii-32-95 9-char keyspace.
     * Nine spaces (0x20 * 9).
     * Change only with intent. */
    memset(plaintext, 0, sizeof(plaintext));
    plaintext_len = 0;
    index_to_plaintext(0, charset, charset_len, 9, 9, pspace_up_to9,
                       plaintext, &plaintext_len);
    ok &= check_plaintext_hex("i2p(0, ascii9)", plaintext, plaintext_len,
                               "202020202020202020", 9);

    /* PINNED FROM IMPL (2026-06-30) — index 999999999 in ascii-32-95 9-char keyspace.
     * Change only with intent. */
    memset(plaintext, 0, sizeof(plaintext));
    plaintext_len = 0;
    index_to_plaintext(999999999, charset, charset_len, 9, 9, pspace_up_to9,
                       plaintext, &plaintext_len);
    ok &= check_plaintext_hex("i2p(999999999, ascii9)", plaintext, plaintext_len,
                               "202020202c3a413e6a", 9);

    return ok;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int test_golden(void)
{
    int ok = 1;

    ok &= test_ntlm();
    ok &= test_hash_to_index();
    ok &= test_index_to_plaintext();

    return ok;
}
