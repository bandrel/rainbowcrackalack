/*
 * Rainbow Crackalack: crackalack_verify.c
 * Copyright (C) 2018-2019  Joe Testa <jtesta@positronsecurity.com>
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

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_rt_functions.h"
#include "markov.h"
#include "mask_parse.h"
#include "misc.h"
#include "terminal_color.h"
#include "verify.h"
#include "version.h"


static int raw_table = 0, quick_table = 0, sorted_table = 0, truncate_on_err = VERIFY_DONT_TRUNCATE;
static struct option long_options[] = {
  {"raw", no_argument, &raw_table, 1},
  {"quick", no_argument, &quick_table, 1},
  {"sorted", no_argument, &sorted_table, 1},
  {"truncate_on_err", no_argument, &truncate_on_err, VERIFY_TRUNCATE_ON_ERROR},
  {"num_chains", required_argument, 0, 'n'},
  {"markov", required_argument, 0, 'm'},
  {"mask", required_argument, 0, 'M'},
  {"challenge", required_argument, 0, 'c'},
  {"custom-charset1", required_argument, 0, '1'},
  {"custom-charset2", required_argument, 0, '2'},
  {"custom-charset3", required_argument, 0, '3'},
  {"custom-charset4", required_argument, 0, '4'},
  {0, 0, 0, 0}
};


void print_usage(char *prog_name) {
  fprintf(stderr, "This program verifies rainbow tables.\n\n\n  %s --raw [--truncate_on_err] [--num_chains X] [--markov FILE] [--mask MASKSTRING [-1 CHARS] [-2 CHARS] [-3 CHARS] [-4 CHARS]] table.rt\n\nThe above command will verify a newly-generated rainbow table.  This ensures that the table 1.) has sequential start points, and 2.) has non-zero ending points.  Optionally, it can truncate_on_err the file to just before the first error found, if any.\n\nWhen --markov is specified, the Markov model is used for CPU chain verification.  When --mask is specified, the mask (hashcat-style, e.g. ?u?l?l?d) is used for CPU chain verification; -1/-2/-3/-4 define custom charset slots.\n\n\n  %s --quick [--markov FILE] [--mask MASKSTRING [-1 CHARS] [-2 CHARS] [-3 CHARS] [-4 CHARS]] table.rt\n\nThe above command will quickly verify a newly-generated rainbow table.  It is similar to using '--raw', but does not examine the start & end points, and only verifies 5 random chains.  As a result, it can do basic verification without needing to read the entire table into memory first (which incurs a huge I/O cost).  The use case for this option is for quickly checking terabytes of tables for sanity.\n\n\n  %s --sorted [--num_chains X] table.rtc\n\nThe above command will verify a sorted rainbow table (i.e.: that it is suitable for lookups).  It ensures that the end indices are sorted in ascending order.  The table may be compressed or uncompressed.\n\n\nIn any case, --num_chains sets the number of random chains to verify using CPU code (hence, providing a large number here will have a dramatic effect on the speed of verification).  Unless overridden, this defaults to 100.\n\nFor NetNTLMv1 tables, --challenge HEX16 sets the 8-byte server challenge used for CPU chain verification (defaults to the standard challenge 1122334455667788).\n\n\n", prog_name, prog_name, prog_name);
}


int main(int ac, char **av) {
  char *filename = NULL;
  char *markov_path = NULL;
  char *mask_str = NULL;
  const char *cc1 = NULL, *cc2 = NULL, *cc3 = NULL, *cc4 = NULL;
  unsigned int table_type = 0;
  int num_chains_to_verify = -1, c = 0, option_index = 0;
  markov_model markov = {0};
  markov_model *markov_ptr = NULL;
  Mask parsed_mask = {0};
  Mask *mask_ptr = NULL;


  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();
  while ((c = getopt_long(ac, av, "1:2:3:4:", long_options, &option_index)) != -1) {
    switch(c) {
    case 0:
      break;
    case 'n':
      num_chains_to_verify = (int)parse_uint_arg(optarg, "--num_chains");
      break;
    case 'm':
      markov_path = optarg;
      break;
    case 'M':
      mask_str = optarg;
      break;
    case '1':
      cc1 = optarg;
      break;
    case '2':
      cc2 = optarg;
      break;
    case '3':
      cc3 = optarg;
      break;
    case '4':
      cc4 = optarg;
      break;
    case 'c': {
      /* NetNTLMv1 CPU chain verification needs the server challenge the table
       * was generated with.  Defaults to the standard challenge otherwise. */
      unsigned char challenge[8];
      if (parse_challenge_str(optarg, challenge) != 0) {
        fprintf(stderr, "Error: --challenge must be exactly 16 hex digits, got '%s'.\n", optarg);
        exit(-1);
      }
      set_netntlmv1_challenge(challenge);
      break;
    }
    default:
      print_usage(av[0]);
      exit(-1);
    }
  }

  /* Only one of --raw, --quick, or --sorted must be specified. */
  if ((raw_table + quick_table + sorted_table) != 1) {
    fprintf(stderr, "\nError: either --raw, --quick, or --sorted must be specified!\n\n");
    print_usage(av[0]);
    exit(-1);
  }

  /* Sorted tables cannot be truncate_on_errd. */
  if (sorted_table && truncate_on_err) {
    fprintf(stderr, "\nError: sorted tables cannot be truncate_on_errd.\n\n");
    exit(-1);
  }

  /* Ensure that one argument remains (i.e.: the filename). */
  if (optind != ac - 1) {
    fprintf(stderr, "\nError: RT/RTC file must be specified!\n\n");
    print_usage(av[0]);
    exit(-1);
  }
  filename = av[optind];

  if (markov_path) {
    if (markov_load(markov_path, &markov) != 0) {
      fprintf(stderr, "Error: failed to load Markov model from '%s'\n", markov_path);
      return -1;
    }
    markov_ptr = &markov;
  }

  if (mask_str) {
    if (mask_parse(mask_str, &parsed_mask, cc1, cc2, cc3, cc4) != 0) {
      fprintf(stderr, "Error: failed to parse mask '%s'\n", mask_str);
      return -1;
    }
    mask_ptr = &parsed_mask;
  }

  if (raw_table)
    table_type = (markov_ptr || mask_ptr) ? VERIFY_TABLE_TYPE_MARKOV : VERIFY_TABLE_TYPE_GENERATED;
  else if (quick_table)
    table_type = VERIFY_TABLE_TYPE_QUICK;
  else if (sorted_table)
    table_type = VERIFY_TABLE_TYPE_LOOKUP;

  if (!verify_rainbowtable_file(filename, table_type, VERIFY_TABLE_IS_COMPLETE, truncate_on_err, num_chains_to_verify, markov_ptr, mask_ptr)) {
    fprintf(stderr, "\n%sRainbow table verification FAILED.%s", REDB, CLR);
    if (truncate_on_err == VERIFY_TRUNCATE_ON_ERROR)
      fprintf(stderr, "  File truncate_on_errd.");
    fprintf(stderr, "\n\n");
    if (markov_ptr)
      markov_free(&markov);
    return -1;
  }

  printf("%sRainbow table successfully verified!%s\n", GREENB, CLR);
  if (markov_ptr)
    markov_free(&markov);
  return 0;
}
