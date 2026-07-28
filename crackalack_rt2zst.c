/*
 * Rainbow Crackalack: crackalack_rt2zst.c
 * Copyright (C) 2026  Justin Bollinger
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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"
#include "zst_compress.h"
#include "zst_decompress.h"

static void usage(const char *prog) {
  fprintf(stderr,
          "Compresses a raw rainbow table (.rt) to zstd (.rt.zst), or back.\n\n"
          "Usage: %s [-l LEVEL] [-T N] [--rm] <input.rt> <output.rt.zst>\n"
          "       %s -d <input.rt.zst> <output.rt>\n\n"
          "  -l LEVEL   zstd compression level 1-22 (default: %d)\n"
          "  -T N       zstd worker threads, 0 = single-threaded (default: 0)\n"
          "  --rm       delete the input file after the output is written\n"
          "  -d         decompress instead of compress\n\n",
          prog, prog, ZST_DEFAULT_LEVEL);
}

int main(int ac, char **av) {
  int level = ZST_DEFAULT_LEVEL, nb_workers = 0, decompress = 0, remove_input = 0;
  char *input = NULL, *output = NULL;
  uint64_t num_chains = 0;
  int i;

  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();

  for (i = 1; i < ac; i++) {
    if (strcmp(av[i], "-d") == 0) {
      decompress = 1;
    } else if (strcmp(av[i], "--rm") == 0) {
      remove_input = 1;
    } else if (strcmp(av[i], "-l") == 0) {
      if (i + 1 >= ac) { usage(av[0]); return -1; }
      level = (int)strtol(av[++i], NULL, 10);
      if (level < 1 || level > 22) {
        fprintf(stderr, "Error: -l must be 1..22.\n");
        return -1;
      }
    } else if (strcmp(av[i], "-T") == 0) {
      if (i + 1 >= ac) { usage(av[0]); return -1; }
      nb_workers = (int)strtol(av[++i], NULL, 10);
      if (nb_workers < 0) nb_workers = 0;
    } else if (input == NULL) {
      input = av[i];
    } else if (output == NULL) {
      output = av[i];
    } else {
      usage(av[0]);
      return -1;
    }
  }

  if (input == NULL || output == NULL) {
    usage(av[0]);
    return -1;
  }

  if (decompress) {
    uint64_t *table = NULL;
    FILE *f = NULL;
    int ret = zst_decompress(input, &table, &num_chains);
    if (ret != 0) {
      fprintf(stderr, "Error while decompressing %s; error code: %d\n", input, ret);
      return -1;
    }
    f = fopen(output, "wb");
    if (f == NULL) {
      fprintf(stderr, "Error: cannot open %s for writing.\n", output);
      free(table);
      return -1;
    }
    if (fwrite(table, sizeof(uint64_t) * 2, num_chains, f) != num_chains) {
      fprintf(stderr, "Error: short write to %s.\n", output);
      fclose(f); free(table);
      return -1;
    }
    fclose(f);
    free(table);
    printf("Decompressed %"PRIu64" chains from \"%s\" to \"%s\".\n",
           num_chains, input, output);
  } else {
    int ret = zst_compress(input, output, level, nb_workers, &num_chains);
    if (ret != 0) {
      fprintf(stderr, "Error while compressing %s; error code: %d\n", input, ret);
      return -1;
    }
    printf("Compressed %"PRIu64" chains in \"%s\" to \"%s\" (level %d).\n",
           num_chains, input, output, level);
  }

  if (remove_input && remove(input) != 0)
    fprintf(stderr, "Warning: output written, but failed to remove %s.\n", input);

  return 0;
}
