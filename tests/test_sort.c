/*
 * Rainbow Crackalack: test_sort.c
 * CPU-only tests for sort_utils.c and parallel_sort.c helper functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "parallel_sort.h"
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

    /* JSP-01: ample RAM and cores; capped by num_files.
     * 8GB / (1GB * 2) = 4 slots, jobs = 4*0.8 = 3, min(3,4) = 3. */
    if (compute_sort_jobs_from_params(8*gb, gb, 8, 4) != 3)
        { fprintf(stderr, "JSP-01 failed\n"); ok = 0; }

    /* JSP-02: capped by cpu_cores before num_files.
     * 16GB / (1GB * 2) = 8 slots, jobs = 8*0.8 = 6, min(6,4) = 4. */
    if (compute_sort_jobs_from_params(16*gb, gb, 4, 10) != 4)
        { fprintf(stderr, "JSP-02 failed\n"); ok = 0; }

    /* JSP-03: RAM-limited: 2 GB free / (2 GB * 2) = 0 slots -> clamped to 1. */
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

    /* JSP-08: 10 GB free / (1 GB * 2) = 5 slots, jobs = 5*0.8 = 4,
     * min(4,6) = 4. */
    if (compute_sort_jobs_from_params(10*gb, gb, 6, 20) != 4)
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


/* --- Group C: parallel_sort_rt --- */

static int verify_sorted(const uint64_t *data, unsigned int n) {
    unsigned int i;
    for (i = 0; i + 1 < n; i++) {
        if (data[i * 2 + 1] > data[(i + 1) * 2 + 1])
            return 0;
    }
    return 1;
}

static int verify_start_indices_preserved(const uint64_t *sorted,
                                           unsigned int n,
                                           uint64_t expected_start_sum) {
    uint64_t sum = 0;
    unsigned int i;
    for (i = 0; i < n; i++)
        sum += sorted[i * 2];
    return sum == expected_start_sum;
}

static int group_c(void)
{
    int ok = 1;

    /* PSR-01: 0 chains. */
    {
        uint64_t dummy[2] = {0, 0};
        if (parallel_sort_rt(dummy, 0, 4) != 0)
            { fprintf(stderr, "PSR-01 failed\n"); ok = 0; }
    }

    /* PSR-02: 1 chain. */
    {
        uint64_t data[2] = {42, 99};
        if (parallel_sort_rt(data, 1, 4) != 0 || data[0] != 42 || data[1] != 99)
            { fprintf(stderr, "PSR-02 failed\n"); ok = 0; }
    }

    /* PSR-03: num_threads > num_chains (small data, falls back to qsort). */
    {
        uint64_t data[6] = {1, 30, 2, 10, 3, 20};
        if (parallel_sort_rt(data, 3, 100) != 0 || !verify_sorted(data, 3))
            { fprintf(stderr, "PSR-03 failed\n"); ok = 0; }
    }

    /* PSR-04: random data, 2048 chains, 4 threads. */
    {
        unsigned int n = 2048;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        uint64_t start_sum = 0;
        unsigned int i;
        srand(12345);
        for (i = 0; i < n; i++) {
            data[i * 2]     = (uint64_t)rand();
            data[i * 2 + 1] = (uint64_t)rand() << 16 | (uint64_t)rand();
            start_sum += data[i * 2];
        }
        if (parallel_sort_rt(data, n, 4) != 0 ||
            !verify_sorted(data, n) ||
            !verify_start_indices_preserved(data, n, start_sum))
            { fprintf(stderr, "PSR-04 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-05: already sorted data. */
    {
        unsigned int n = 2048;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        unsigned int i;
        for (i = 0; i < n; i++) {
            data[i * 2]     = i + 100;
            data[i * 2 + 1] = i;
        }
        if (parallel_sort_rt(data, n, 4) != 0 || !verify_sorted(data, n))
            { fprintf(stderr, "PSR-05 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-06: reverse sorted data. */
    {
        unsigned int n = 2048;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        unsigned int i;
        for (i = 0; i < n; i++) {
            data[i * 2]     = i;
            data[i * 2 + 1] = n - 1 - i;
        }
        if (parallel_sort_rt(data, n, 4) != 0 || !verify_sorted(data, n))
            { fprintf(stderr, "PSR-06 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-07: all duplicate endpoints. */
    {
        unsigned int n = 2048;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        unsigned int i;
        for (i = 0; i < n; i++) {
            data[i * 2]     = i;
            data[i * 2 + 1] = 42;
        }
        if (parallel_sort_rt(data, n, 4) != 0 || !verify_sorted(data, n))
            { fprintf(stderr, "PSR-07 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-08: uneven chunks - 2049 chains / 4 threads. */
    {
        unsigned int n = 2049;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        unsigned int i;
        srand(67890);
        for (i = 0; i < n; i++) {
            data[i * 2]     = (uint64_t)rand();
            data[i * 2 + 1] = (uint64_t)rand() << 16 | (uint64_t)rand();
        }
        if (parallel_sort_rt(data, n, 4) != 0 || !verify_sorted(data, n))
            { fprintf(stderr, "PSR-08 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-09: uneven chunks - 1025 chains / 3 threads. */
    {
        unsigned int n = 1025;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        unsigned int i;
        srand(11111);
        for (i = 0; i < n; i++) {
            data[i * 2]     = (uint64_t)rand();
            data[i * 2 + 1] = (uint64_t)rand() << 16 | (uint64_t)rand();
        }
        if (parallel_sort_rt(data, n, 3) != 0 || !verify_sorted(data, n))
            { fprintf(stderr, "PSR-09 failed\n"); ok = 0; }
        free(data);
    }

    /* PSR-10: stress test - 100K chains, 8 threads. */
    {
        unsigned int n = 100000;
        uint64_t *data = malloc(n * 2 * sizeof(uint64_t));
        uint64_t start_sum = 0;
        unsigned int i;
        srand(99999);
        for (i = 0; i < n; i++) {
            data[i * 2]     = (uint64_t)rand() << 32 | (uint64_t)rand();
            data[i * 2 + 1] = (uint64_t)rand() << 32 | (uint64_t)rand();
            start_sum += data[i * 2];
        }
        if (parallel_sort_rt(data, n, 8) != 0 ||
            !verify_sorted(data, n) ||
            !verify_start_indices_preserved(data, n, start_sum))
            { fprintf(stderr, "PSR-10 failed\n"); ok = 0; }
        free(data);
    }

    return ok;
}


int test_sort(void)
{
    int ok = 1;

    ok &= group_a();
    ok &= group_b();
    ok &= group_c();

    return ok;
}
