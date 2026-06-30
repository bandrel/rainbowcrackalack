#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parallel_sort.h"

#define NUM_CHAINS  1000000
#define NUM_THREADS 8
#define NUM_ROUNDS  3

int main(void) {
    uint64_t *data = malloc(NUM_CHAINS * 2 * sizeof(uint64_t));
    if (!data) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }

    /* Use a deterministic seed so the test is reproducible. */
    srand(42);

    for (int round = 1; round <= NUM_ROUNDS; round++) {
        /* Fill with random start/end pairs. */
        for (uint64_t i = 0; i < NUM_CHAINS; i++) {
            data[i * 2 + 0] = ((uint64_t)rand() << 32) | (uint64_t)rand();
            data[i * 2 + 1] = ((uint64_t)rand() << 32) | (uint64_t)rand();
        }

        int rc = parallel_sort_rt(data, NUM_CHAINS, NUM_THREADS);
        if (rc != 0) {
            fprintf(stderr, "parallel_sort_rt returned %d on round %d\n", rc, round);
            free(data);
            return 1;
        }

        /* Verify sorted order by end index (data[i*2+1]). */
        int ok = 1;
        for (uint64_t i = 1; i < NUM_CHAINS; i++) {
            if (data[(i - 1) * 2 + 1] > data[i * 2 + 1]) {
                fprintf(stderr, "sort FAILED at index %llu on round %d: "
                        "data[%llu]=%llu > data[%llu]=%llu\n",
                        (unsigned long long)i, round,
                        (unsigned long long)(i - 1),
                        (unsigned long long)data[(i - 1) * 2 + 1],
                        (unsigned long long)i,
                        (unsigned long long)data[i * 2 + 1]);
                ok = 0;
                break;
            }
        }

        if (!ok) {
            free(data);
            return 1;
        }

        printf("sorted OK (round %d)\n", round);
        fflush(stdout);
    }

    free(data);
    printf("ALL ROUNDS SORTED CLEAN\n");
    return 0;
}
