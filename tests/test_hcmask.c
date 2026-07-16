/* CPU-only unit tests for hcmask.h / hcmask.c */
#include <stdio.h>
#include <string.h>
#include "hcmask.h"
#include "test_hcmask.h"

static int group_parse_line(void)
{
    int ok = 1;
    HcmaskEntry e;

    /* HM-01: plain mask, no custom charsets */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("?u?l?l?l?l?l?d", &e) != 1 ||
        strcmp(e.mask, "?u?l?l?l?l?l?d") != 0 ||
        e.has_cc[0] || e.has_cc[1] || e.has_cc[2] || e.has_cc[3]) {
        fprintf(stderr, "HM-01 failed: plain mask\n"); ok = 0;
    }

    /* HM-02: one inline custom charset: "?d?l,?1?1?1?1" */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("?d?l,?1?1?1?1", &e) != 1 ||
        strcmp(e.mask, "?1?1?1?1") != 0 ||
        !e.has_cc[0] || strcmp(e.cc[0], "?d?l") != 0 ||
        e.has_cc[1]) {
        fprintf(stderr, "HM-02 failed: one inline charset\n"); ok = 0;
    }

    /* HM-03: two inline charsets: "abc,def,?1?2?1?2" */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("abc,def,?1?2?1?2", &e) != 1 ||
        strcmp(e.mask, "?1?2?1?2") != 0 ||
        !e.has_cc[0] || strcmp(e.cc[0], "abc") != 0 ||
        !e.has_cc[1] || strcmp(e.cc[1], "def") != 0) {
        fprintf(stderr, "HM-03 failed: two inline charsets\n"); ok = 0;
    }

    /* HM-04: escaped comma "\," is literal, not a separator: "a\,b,?1?1" */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("a\\,b,?1?1", &e) != 1 ||
        strcmp(e.mask, "?1?1") != 0 ||
        !e.has_cc[0] || strcmp(e.cc[0], "a,b") != 0) {
        fprintf(stderr, "HM-04 failed: escaped comma (cc0=\"%s\")\n", e.cc[0]); ok = 0;
    }

    /* HM-05: leading "\#" is NOT a comment; '#' kept as literal in charset */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("\\#x,?1?1?1?1", &e) != 1 ||
        strcmp(e.mask, "?1?1?1?1") != 0 ||
        !e.has_cc[0] || strcmp(e.cc[0], "#x") != 0) {
        fprintf(stderr, "HM-05 failed: escaped hash (cc0=\"%s\")\n", e.cc[0]); ok = 0;
    }

    /* HM-06: comment line -> skipped (return 0) */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("# this is a comment", &e) != 0) {
        fprintf(stderr, "HM-06 failed: comment not skipped\n"); ok = 0;
    }

    /* HM-07: blank / whitespace-only -> skipped */
    if (hcmask_parse_line("   ", &e) != 0 || hcmask_parse_line("", &e) != 0) {
        fprintf(stderr, "HM-07 failed: blank not skipped\n"); ok = 0;
    }

    /* HM-08: too many fields (6) -> error */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("a,b,c,d,e,?1?1", &e) != -1) {
        fprintf(stderr, "HM-08 failed: 6 fields accepted\n"); ok = 0;
    }

    /* HM-09: empty mask field -> error ("abc," has empty last field) */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("abc,", &e) != -1) {
        fprintf(stderr, "HM-09 failed: empty mask accepted\n"); ok = 0;
    }

    /* HM-10: \xNN hex passes through untouched to the charset field */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("\\x41\\x42,?1?1", &e) != 1 ||
        strcmp(e.cc[0], "\\x41\\x42") != 0 || strcmp(e.mask, "?1?1") != 0) {
        fprintf(stderr, "HM-10 failed: hex passthrough (cc0=\"%s\")\n", e.cc[0]); ok = 0;
    }

    /* HM-11: trailing newline is stripped */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("?d?d?d?d\n", &e) != 1 || strcmp(e.mask, "?d?d?d?d") != 0) {
        fprintf(stderr, "HM-11 failed: newline not stripped (mask=\"%s\")\n", e.mask); ok = 0;
    }

    /* HM-12: four inline custom charsets (the maximum): "a,b,c,d,?1?2?3?4" */
    memset(&e, 0, sizeof(e));
    if (hcmask_parse_line("a,b,c,d,?1?2?3?4", &e) != 1 ||
        strcmp(e.mask, "?1?2?3?4") != 0 ||
        !e.has_cc[0] || strcmp(e.cc[0], "a") != 0 ||
        !e.has_cc[1] || strcmp(e.cc[1], "b") != 0 ||
        !e.has_cc[2] || strcmp(e.cc[2], "c") != 0 ||
        !e.has_cc[3] || strcmp(e.cc[3], "d") != 0) {
        fprintf(stderr, "HM-12 failed: four inline charsets\n"); ok = 0;
    }

    return ok;
}

int test_hcmask(void)
{
    int ok = 1;
    if (!group_parse_line()) ok = 0;
    return ok;
}
