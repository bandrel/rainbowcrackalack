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

#include <dirent.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_rt_functions.h"
#include "hcmask.h"
#include "markov.h"
#include "markov_mask.h"
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
  {"hcmask", required_argument, 0, 'K'},
  {"challenge", required_argument, 0, 'c'},
  {"custom-charset1", required_argument, 0, '1'},
  {"custom-charset2", required_argument, 0, '2'},
  {"custom-charset3", required_argument, 0, '3'},
  {"custom-charset4", required_argument, 0, '4'},
  {0, 0, 0, 0}
};


void print_usage(char *prog_name) {
  fprintf(stderr, "This program verifies rainbow tables.\n\n\n  %s --raw [--truncate_on_err] [--num_chains X] [--markov FILE] [--mask MASKSTRING [-1 CHARS] [-2 CHARS] [-3 CHARS] [-4 CHARS]] table.rt\n\nThe above command will verify a newly-generated rainbow table.  This ensures that the table 1.) has sequential start points, and 2.) has non-zero ending points.  Optionally, it can truncate_on_err the file to just before the first error found, if any.\n\nWhen --markov is specified, the Markov model is used for CPU chain verification.  When --mask is specified, the mask (hashcat-style, e.g. ?u?l?l?d) is used for CPU chain verification; -1/-2/-3/-4 define custom charset slots.\n\n\n  %s --quick [--markov FILE] [--mask MASKSTRING [-1 CHARS] [-2 CHARS] [-3 CHARS] [-4 CHARS]] table.rt\n\nThe above command will quickly verify a newly-generated rainbow table.  It is similar to using '--raw', but does not examine the start & end points, and only verifies 5 random chains.  As a result, it can do basic verification without needing to read the entire table into memory first (which incurs a huge I/O cost).  The use case for this option is for quickly checking terabytes of tables for sanity.\n\n\n  %s --sorted [--num_chains X] table.rtc\n\nThe above command will verify a sorted rainbow table (i.e.: that it is suitable for lookups).  It ensures that the end indices are sorted in ascending order.  The table may be compressed or uncompressed.\n\n\nIn any case, --num_chains sets the number of random chains to verify using CPU code (hence, providing a large number here will have a dramatic effect on the speed of verification).  Unless overridden, this defaults to 100.\n\nFor NetNTLMv1 tables, --challenge HEX16 sets the 8-byte server challenge used for CPU chain verification (defaults to the standard challenge 1122334455667788).\n\n\n  %s --hcmask FILE table_dir/\n\nThe above command batch-verifies a mask campaign: for each mask line in the .hcmask FILE, it locates the matching table(s) in table_dir/, verifies each (quick mode), and reports any mask that has no matching table (MISSING).  Returns non-zero if any entry is missing or any table fails.\n\n\n", prog_name, prog_name, prog_name, prog_name);
}


/* Batch-verify every mask in an .hcmask file against tables in a directory.
 * For each mask, reconstruct the filename charset field, find matching .rt
 * table(s), verify each, and report masks with no matching table.
 * Returns 0 if all entries verified & present, non-zero otherwise. */
static int verify_hcmask_batch(const char *hcmask_path, const char *dir,
                               const char *g1, const char *g2,
                               const char *g3, const char *g4) {
  HcmaskEntry *entries = NULL;
  int nentries = 0, e, rc = 0;

  if (hcmask_load(hcmask_path, &entries, &nentries) != 0)
    return 1;

  for (e = 0; e < nentries; e++) {
    /* Inline per-mask custom charsets win; otherwise fall back to the global
     * -1..-4 flags (matching how crackalack_gen resolves them). */
    const char *rc1 = entries[e].has_cc[0] ? entries[e].cc[0] : g1;
    const char *rc2 = entries[e].has_cc[1] ? entries[e].cc[1] : g2;
    const char *rc3 = entries[e].has_cc[2] ? entries[e].cc[2] : g3;
    const char *rc4 = entries[e].has_cc[3] ? entries[e].cc[3] : g4;
    Mask m = {0};
    char field[256], prefix[512];
    DIR *d;
    struct dirent *de;
    int found = 0;

    printf("\nMask %d/%d: '%s'\n", e + 1, nentries, entries[e].mask);

    if (mask_parse(entries[e].mask, &m, rc1, rc2, rc3, rc4) != 0) {
      fprintf(stderr, "hcmask entry %d: bad mask '%s'\n", e + 1, entries[e].mask);
      rc = 1; continue;
    }
    if (mask_encode_charset_field(entries[e].mask, rc1, rc2, rc3, rc4,
                                  field, sizeof(field)) != 0) {
      fprintf(stderr, "hcmask entry %d: cannot encode mask\n", e + 1);
      rc = 1; continue;
    }
    /* Match "_<field>#<len>-<len>_" as a filename fragment.  Distinct per mask
     * because mask_encode_charset_field round-trips each field uniquely. */
    snprintf(prefix, sizeof(prefix), "_%s#%d-%d_", field, m.length, m.length);

    d = opendir(dir);
    if (!d) { perror("opendir"); free(entries); return 1; }
    while ((de = readdir(d)) != NULL) {
      char path[4096];
      size_t nlen = strlen(de->d_name);
      if (nlen < 3 || strcmp(de->d_name + nlen - 3, ".rt") != 0) continue;
      if (strstr(de->d_name, prefix) == NULL) continue;
      found = 1;
      snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
      /* Verify this table using quick mode (5 random chains via CPU).  We pass
       * the parsed mask so verify walks chains with generate_rainbow_chain_mask;
       * the quick path has no NULL-mask fallback for mask tables. */
      if (verify_rainbowtable_file(path, VERIFY_TABLE_TYPE_QUICK, VERIFY_TABLE_IS_COMPLETE, VERIFY_DONT_TRUNCATE, -1, NULL, &m, NULL)) {
        printf("  %sOK%s: %s\n", GREENB, CLR, de->d_name);
      } else {
        printf("  %sFAIL%s: %s\n", REDB, CLR, de->d_name);
        rc = 1;
      }
    }
    closedir(d);
    if (!found) {
      printf("  %sMISSING%s: no table for mask '%s' (match %s*)\n", REDB, CLR, entries[e].mask, prefix);
      rc = 1;
    }
  }
  free(entries);
  return rc;
}


int main(int ac, char **av) {
  char *filename = NULL;
  char *markov_path = NULL;
  char *mask_str = NULL;
  const char *hcmask_path = NULL;
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
    case 'K':
      hcmask_path = optarg;
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

  /* Batch mode: --hcmask FILE DIR verifies every mask in the .hcmask file
   * against tables in DIR and reports masks with no matching table. */
  if (hcmask_path) {
    if (optind != ac - 1) {
      fprintf(stderr, "\nError: --hcmask requires a table directory argument!\n\n");
      print_usage(av[0]);
      exit(-1);
    }
    if (mask_str || markov_path) {
      fprintf(stderr, "Error: --hcmask is mutually exclusive with --mask and --markov.\n");
      exit(-1);
    }
    return verify_hcmask_batch(hcmask_path, av[optind], cc1, cc2, cc3, cc4);
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

  /* Combined mask+Markov verification: build the restricted tables so chain
   * regeneration uses the mixed-radix Markov decode.  Requires both flags. */
  markov_mask_tables mmtables = {0};
  markov_mask_tables *mmtables_ptr = NULL;
  if (markov_ptr && mask_ptr) {
    if (markov_build_restricted(markov_ptr, mask_ptr, &mmtables) != 0) {
      fprintf(stderr, "Error: failed to build combined mask+Markov tables (mask must be a subset of the model charset).\n");
      markov_free(&markov);
      return -1;
    }
    mmtables_ptr = &mmtables;
  }

  if (raw_table)
    table_type = (markov_ptr || mask_ptr) ? VERIFY_TABLE_TYPE_MARKOV : VERIFY_TABLE_TYPE_GENERATED;
  else if (quick_table)
    table_type = VERIFY_TABLE_TYPE_QUICK;
  else if (sorted_table)
    table_type = VERIFY_TABLE_TYPE_LOOKUP;

  if (!verify_rainbowtable_file(filename, table_type, VERIFY_TABLE_IS_COMPLETE, truncate_on_err, num_chains_to_verify, markov_ptr, mask_ptr, mmtables_ptr)) {
    fprintf(stderr, "\n%sRainbow table verification FAILED.%s", REDB, CLR);
    if (truncate_on_err == VERIFY_TRUNCATE_ON_ERROR)
      fprintf(stderr, "  File truncate_on_errd.");
    fprintf(stderr, "\n\n");
    if (mmtables_ptr)
      markov_mask_tables_free(&mmtables);
    if (markov_ptr)
      markov_free(&markov);
    return -1;
  }

  printf("%sRainbow table successfully verified!%s\n", GREENB, CLR);
  if (mmtables_ptr)
    markov_mask_tables_free(&mmtables);
  if (markov_ptr)
    markov_free(&markov);
  return 0;
}
