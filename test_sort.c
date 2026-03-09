/*
 * Rainbow Crackalack: test_sort.c
 * CPU-only tests for sort_utils.c helper functions.
 */

#include <stdio.h>
#include <stdint.h>

#include "sort_utils.h"
#include "test_sort.h"


/* --- Group A: is_sorted_rt --- */
static int group_a(void)
{
    int ok = 1;
    uint64_t data[12] = {0};

    /* ISR-01: empty array is sorted. */
    if (is_sorted_rt(data, 0) != 1)
        { fprintf(stderr, "ISR-01 failed\n"); ok = 0; }

    /* ISR-02: single chain is sorted. */
    data[0] = 111; data[1] = 42;
    if (is_sorted_rt(data, 1) != 1)
        { fprintf(stderr, "ISR-02 failed\n"); ok = 0; }

    /* ISR-03: two chains in ascending order. */
    data[0] = 0; data[1] = 10;
    data[2] = 0; data[3] = 20;
    if (is_sorted_rt(data, 2) != 1)
        { fprintf(stderr, "ISR-03 failed\n"); ok = 0; }

    /* ISR-04: two chains in descending order (unsorted). */
    data[0] = 0; data[1] = 20;
    data[2] = 0; data[3] = 10;
    if (is_sorted_rt(data, 2) != 0)
        { fprintf(stderr, "ISR-04 failed\n"); ok = 0; }

    /* ISR-05: equal adjacent endpoints count as sorted. */
    data[0] = 0; data[1] = 10;
    data[2] = 0; data[3] = 10;
    if (is_sorted_rt(data, 2) != 1)
        { fprintf(stderr, "ISR-05 failed\n"); ok = 0; }

    /* ISR-06: three chains, violation at the end. */
    data[0] = 0; data[1] = 10;
    data[2] = 0; data[3] = 20;
    data[4] = 0; data[5] = 15;
    if (is_sorted_rt(data, 3) != 0)
        { fprintf(stderr, "ISR-06 failed\n"); ok = 0; }

    /* ISR-07: three chains, violation at the start. */
    data[0] = 0; data[1] = 30;
    data[2] = 0; data[3] = 20;
    data[4] = 0; data[5] = 25;
    if (is_sorted_rt(data, 3) != 0)
        { fprintf(stderr, "ISR-07 failed\n"); ok = 0; }

    /* ISR-08: start indices are ignored; only end indices matter. */
    data[0] = 999; data[1] = 5;
    data[2] = 1;   data[3] = 10;
    data[4] = 500; data[5] = 15;
    if (is_sorted_rt(data, 3) != 1)
        { fprintf(stderr, "ISR-08 failed\n"); ok = 0; }

    /* ISR-09: UINT64_MAX as an endpoint. */
    data[0] = 0; data[1] = UINT64_MAX - 1;
    data[2] = 0; data[3] = UINT64_MAX;
    if (is_sorted_rt(data, 2) != 1)
        { fprintf(stderr, "ISR-09 failed\n"); ok = 0; }

    /* ISR-10: all endpoints == 0 is a valid sorted state. */
    data[0] = 5; data[1] = 0;
    data[2] = 9; data[3] = 0;
    if (is_sorted_rt(data, 2) != 1)
        { fprintf(stderr, "ISR-10 failed\n"); ok = 0; }

    /* ISR-11: all endpoints == UINT64_MAX is a valid sorted state. */
    data[0] = 0; data[1] = UINT64_MAX;
    data[2] = 0; data[3] = UINT64_MAX;
    if (is_sorted_rt(data, 2) != 1)
        { fprintf(stderr, "ISR-11 failed\n"); ok = 0; }

    return ok;
}


/* --- Group B: compute_sort_jobs_from_params --- */
static int group_b(void)
{
    int ok = 1;
    uint64_t gb = (uint64_t)1024 * 1024 * 1024;

    /* JSP-01: ample RAM and cores; capped by num_files. */
    if (compute_sort_jobs_from_params(8*gb, gb, 8, 4) != 4)
        { fprintf(stderr, "JSP-01 failed\n"); ok = 0; }

    /* JSP-02: capped by cpu_cores before num_files. */
    if (compute_sort_jobs_from_params(16*gb, gb, 4, 10) != 4)
        { fprintf(stderr, "JSP-02 failed\n"); ok = 0; }

    /* JSP-03: RAM-limited: 2 GB free / 2 GB file = 0.8 jobs -> clamped to 1. */
    if (compute_sort_jobs_from_params(2*gb, 2*gb, 16, 10) != 1)
        { fprintf(stderr, "JSP-03 failed\n"); ok = 0; }

    /* JSP-04: zero max_file_size -> 1. */
    if (compute_sort_jobs_from_params(8*gb, 0, 8, 5) != 1)
        { fprintf(stderr, "JSP-04 failed\n"); ok = 0; }

    /* JSP-05: zero free_ram -> 1. */
    if (compute_sort_jobs_from_params(0, gb, 8, 5) != 1)
        { fprintf(stderr, "JSP-05 failed\n"); ok = 0; }

    /* JSP-06: single file -> always 1 regardless of resources. */
    if (compute_sort_jobs_from_params(64*gb, gb, 32, 1) != 1)
        { fprintf(stderr, "JSP-06 failed\n"); ok = 0; }

    /* JSP-07: RAM allows more than cores allow; capped at cores. */
    if (compute_sort_jobs_from_params(64*gb, gb, 3, 20) != 3)
        { fprintf(stderr, "JSP-07 failed\n"); ok = 0; }

    /* JSP-08: file exactly fills 80% boundary - 10 GB free, 1 GB file.
     * (10 * 0.8) / 1 = 8 jobs; capped by 6 cores -> 6. */
    if (compute_sort_jobs_from_params(10*gb, gb, 6, 20) != 6)
        { fprintf(stderr, "JSP-08 failed\n"); ok = 0; }

    /* JSP-09: cpu_cores == 0 is clamped to 1. */
    if (compute_sort_jobs_from_params(8*gb, gb, 0, 5) != 1)
        { fprintf(stderr, "JSP-09 failed\n"); ok = 0; }

    /* JSP-10: max_file_size > free_ram -> 1. */
    if (compute_sort_jobs_from_params(gb, 2*gb, 8, 5) != 1)
        { fprintf(stderr, "JSP-10 failed\n"); ok = 0; }

    /* JSP-11: single CPU core -> 1 regardless of RAM. */
    if (compute_sort_jobs_from_params(64*gb, gb, 1, 100) != 1)
        { fprintf(stderr, "JSP-11 failed\n"); ok = 0; }

    /* JSP-12: near-overflow free_ram must not wrap to 0 or below. */
    if (compute_sort_jobs_from_params(UINT64_MAX / 4, (uint64_t)1 << 30, 16, 20) < 1)
        { fprintf(stderr, "JSP-12 failed\n"); ok = 0; }

    return ok;
}


int test_sort(void)
{
    int ok = 1;

    ok &= group_a();
    ok &= group_b();

    return ok;
}
