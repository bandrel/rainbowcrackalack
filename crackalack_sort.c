/*
 * Rainbow Crackalack: crackalack_sort.c
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

#ifdef _WIN32
#include <windows.h>
#endif
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "terminal_color.h"
#include "version.h"

#define CHAIN_SIZE (unsigned int)(sizeof(uint64_t) * 2)


static int compare_by_end_index(const void *a, const void *b) {
  uint64_t end_a = ((const uint64_t *)a)[1];
  uint64_t end_b = ((const uint64_t *)b)[1];
  if (end_a < end_b) return -1;
  if (end_a > end_b) return 1;
  return 0;
}


static int sort_file(const char *filename) {
  FILE *f = NULL;
  uint64_t *data = NULL;
  long file_size = 0;
  unsigned int num_chains = 0;
  int ret = -1;

  f = fopen(filename, "rb");
  if (f == NULL) {
    fprintf(stderr, "%sError: failed to open %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    return -1;
  }

  fseek(f, 0, SEEK_END);
  file_size = ftell(f);

  if (file_size <= 0) {
    fprintf(stderr, "%sError: %s is empty or unreadable.%s\n", REDB, filename, CLR);
    goto done;
  }

  if ((file_size % CHAIN_SIZE) != 0) {
    fprintf(stderr, "%sError: %s size (%" PRId64 ") is not aligned to %u bytes. File may be compressed or corrupt.%s\n",
            REDB, filename, (int64_t)file_size, CHAIN_SIZE, CLR);
    goto done;
  }

  num_chains = (unsigned int)(file_size / CHAIN_SIZE);

  data = malloc((size_t)file_size);
  if (data == NULL) {
    fprintf(stderr, "%sError: failed to allocate %" PRId64 " bytes for %s: %s%s\n",
            REDB, (int64_t)file_size, filename, strerror(errno), CLR);
    goto done;
  }

  fseek(f, 0, SEEK_SET);
  if (fread(data, CHAIN_SIZE, num_chains, f) != num_chains) {
    fprintf(stderr, "%sError: failed to read %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }
  fclose(f);
  f = NULL;

  printf("Sorting %s (%u chains)... ", filename, num_chains);
  fflush(stdout);

  qsort(data, num_chains, CHAIN_SIZE, compare_by_end_index);

  f = fopen(filename, "wb");
  if (f == NULL) {
    fprintf(stderr, "\n%sError: failed to open %s for writing: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }

  if (fwrite(data, CHAIN_SIZE, num_chains, f) != num_chains) {
    fprintf(stderr, "\n%sError: failed to write %s: %s%s\n", REDB, filename, strerror(errno), CLR);
    goto done;
  }

  printf("%sdone.%s\n", GREENB, CLR);
  ret = 0;

done:
  if (f != NULL)
    fclose(f);
  free(data);
  return ret;
}


int main(int ac, char **av) {
  int i = 0;
  int failures = 0;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  if (ac < 2) {
    printf("Sorts rainbow tables by end index for use with crackalack_lookup.\n\nUsage: %s table1.rt [table2.rt ...]\n\n", av[0]);
    return 0;
  }

  for (i = 1; i < ac; i++) {
    if (sort_file(av[i]) != 0)
      failures++;
  }

  if (failures > 0) {
    fprintf(stderr, "\n%s%d file(s) failed to sort.%s\n", REDB, failures, CLR);
    return 1;
  }

  return 0;
}
