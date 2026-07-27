/*
 * Rainbow Crackalack: test_chain_writer.c
 *
 * Tests that write_chains() places chains at the correct file offset.
 *
 * The offset of a chain within a table file is determined by the chain index
 * stored at offset 0 -- which is (total_chains_in_table * part_index) -- and
 * NOT by the first chain this particular run happens to generate.  Conflating
 * the two silently destroys the existing contents of a table that is being
 * resumed, because the first chain generated after the resume lands at offset
 * zero instead of at the end of the file.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "chain_writer.h"
#include "misc.h"
#include "shared.h"
#include "test_chain_writer.h"


#define TEST_TABLE_PATH "/tmp/crackalack_test_chain_writer.rt"


/* Reads chain number 'n' out of the table file.  Returns 0 on success. */
static int read_chain(const char *path, uint64_t n, uint64_t *start_out, uint64_t *end_out) {
  FILE *f = fopen(path, "rb");
  int ret = -1;

  if (f == NULL)
    return -1;

  if (fseek(f, (long)(n * CHAIN_SIZE), SEEK_SET) == 0 &&
      fread(start_out, sizeof(uint64_t), 1, f) == 1 &&
      fread(end_out, sizeof(uint64_t), 1, f) == 1)
    ret = 0;

  fclose(f);
  return ret;
}


static uint64_t file_size_of(const char *path) {
  FILE *f = fopen(path, "rb");
  long size = 0;

  if (f == NULL)
    return 0;

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fclose(f);
  return (uint64_t)size;
}


/* Writes 'n' chains to a fresh file, so we have something to resume from.  Chain
 * i holds start index (base + i) and end index (0xe000 + i) so that clobbered
 * data is easy to spot. */
static int seed_table(const char *path, uint64_t base, uint64_t n) {
  FILE *f = fopen(path, "wb");
  uint64_t i = 0, start = 0, end = 0;

  if (f == NULL)
    return -1;

  for (i = 0; i < n; i++) {
    start = base + i;
    end = 0xe000 + i;
    if ((fwrite(&start, sizeof(start), 1, f) != 1) ||
        (fwrite(&end, sizeof(end), 1, f) != 1)) {
      fclose(f);
      return -1;
    }
  }

  fclose(f);
  return 0;
}


static void cleanup(const char *path) {
  char log_path[512] = {0};

  unlink(path);
  snprintf(log_path, sizeof(log_path) - 1, "%s.log", path);
  unlink(log_path);
}


/* A table that is resumed must have the new chains appended, leaving the chains
 * already in the file untouched. */
static int test_resume_appends_without_clobbering(void) {
  uint64_t start_indices[4] = {256, 257, 258, 259};
  uint64_t end_indices[4] = {0xf000, 0xf001, 0xf002, 0xf003};
  uint64_t start = 0, end = 0;
  unsigned int i = 0;
  int failed = 0;

  cleanup(TEST_TABLE_PATH);

  /* The file already holds chains 0 through 255 from an earlier run. */
  if (seed_table(TEST_TABLE_PATH, 0, 256) != 0) {
    fprintf(stderr, "test_chain_writer: failed to seed table\n");
    return -1;
  }

  /* Part index 0, so the chain at file offset 0 is chain 0... */
  file_base_chain = 0;

  /* ...but this run resumes, so it starts generating at chain 256. */
  first_generated_chain = 256;

  write_chains(TEST_TABLE_PATH, 1, start_indices, 4, end_indices, 4, 0);

  /* The four new chains must land at chains 256-259. */
  for (i = 0; i < 4; i++) {
    if (read_chain(TEST_TABLE_PATH, 256 + i, &start, &end) != 0) {
      fprintf(stderr, "test_chain_writer: could not read chain %u\n", 256 + i);
      failed = 1;
      break;
    }
    if ((start != start_indices[i]) || (end != end_indices[i])) {
      fprintf(stderr, "test_chain_writer: chain %u is (%"PRIu64", 0x%"PRIx64"); expected (%"PRIu64", 0x%"PRIx64")\n",
              256 + i, start, end, start_indices[i], end_indices[i]);
      failed = 1;
    }
  }

  /* Every chain that was already in the file must be untouched. */
  for (i = 0; i < 256; i++) {
    if (read_chain(TEST_TABLE_PATH, i, &start, &end) != 0) {
      fprintf(stderr, "test_chain_writer: could not read pre-existing chain %u\n", i);
      failed = 1;
      break;
    }
    if ((start != i) || (end != (0xe000 + i))) {
      fprintf(stderr, "test_chain_writer: pre-existing chain %u was clobbered: got (%"PRIu64", 0x%"PRIx64"); expected (%u, 0x%x)\n",
              i, start, end, i, 0xe000 + i);
      failed = 1;
      break;
    }
  }

  if (file_size_of(TEST_TABLE_PATH) != (260 * CHAIN_SIZE)) {
    fprintf(stderr, "test_chain_writer: table is %"PRIu64" bytes; expected %u\n",
            file_size_of(TEST_TABLE_PATH), 260 * CHAIN_SIZE);
    failed = 1;
  }

  cleanup(TEST_TABLE_PATH);
  return failed ? -1 : 0;
}


/* Part index > 0 means chain (total_chains * part_index) sits at file offset 0,
 * so a fresh part file must start writing at offset 0, not far past it. */
static int test_nonzero_part_index_starts_at_offset_zero(void) {
  uint64_t start_indices[2] = {5000, 5001};
  uint64_t end_indices[2] = {0xabc0, 0xabc1};
  uint64_t start = 0, end = 0;
  int failed = 0;

  cleanup(TEST_TABLE_PATH);

  /* An empty part file: part 1 of a table with 5000 chains per part. */
  if (seed_table(TEST_TABLE_PATH, 0, 0) != 0) {
    fprintf(stderr, "test_chain_writer: failed to create empty table\n");
    return -1;
  }

  file_base_chain = 5000;
  first_generated_chain = 5000;

  write_chains(TEST_TABLE_PATH, 1, start_indices, 2, end_indices, 2, 0);

  if (read_chain(TEST_TABLE_PATH, 0, &start, &end) != 0) {
    fprintf(stderr, "test_chain_writer: could not read chain 0 of part file\n");
    failed = 1;
  } else if ((start != 5000) || (end != 0xabc0)) {
    fprintf(stderr, "test_chain_writer: part file chain 0 is (%"PRIu64", 0x%"PRIx64"); expected (5000, 0xabc0)\n", start, end);
    failed = 1;
  }

  if (file_size_of(TEST_TABLE_PATH) != (2 * CHAIN_SIZE)) {
    fprintf(stderr, "test_chain_writer: part file is %"PRIu64" bytes; expected %u\n",
            file_size_of(TEST_TABLE_PATH), 2 * CHAIN_SIZE);
    failed = 1;
  }

  cleanup(TEST_TABLE_PATH);
  return failed ? -1 : 0;
}


int test_chain_writer(void) {
  int all_passed = 1;

  printf("Testing chain writer file offsets...\n"); fflush(stdout);

  if (test_resume_appends_without_clobbering() != 0) {
    fprintf(stderr, "\tresume append: FAILED\n");
    all_passed = 0;
  } else
    printf("\tresume append: passed\n");

  if (test_nonzero_part_index_starts_at_offset_zero() != 0) {
    fprintf(stderr, "\tnon-zero part index: FAILED\n");
    all_passed = 0;
  } else
    printf("\tnon-zero part index: passed\n");

  fflush(stdout);
  return all_passed;
}
