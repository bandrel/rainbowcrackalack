/*
 * Rainbow Crackalack: chain_writer.c
 * Copyright (C) 2018-2020  Joe Testa <jtesta@positronsecurity.com>
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
#include <unistd.h>

#include "chain_writer.h"
#include "file_lock.h"
#include "misc.h"
#include "shared.h"


/* See chain_writer.h for the distinction between these two. */
uint64_t first_generated_chain = 0;
uint64_t file_base_chain = 0;


/* Writes the chains given by the kernel to the file. */
void write_chains(char *filename, unsigned int chains_per_work_unit, uint64_t *start_indices, unsigned int start_indices_size, uint64_t *end_indices, unsigned int end_indices_size, unsigned int thread_id) {
  int i = 0, j = 0;
  uint64_t file_size = 0;
  uint64_t start = 0;
  rc_file f = rc_fopen(filename, 0), l = NULL;
  char log_filename[256] = {0};
  int empty_chains = 0;


  if (f == NULL)
    exit(-1);

  /* Get an exclusive lock on all bytes of the file, including those not yet written
   * (i.e.: another thread cannot write past the current end of the file).  */
  if (rc_flock(f) != 0)
    exit(-1);

  /* Get the filename of the rainbow table log to write to, then open it for appending.
   *  This is the same filename as the rainbow table, but with ".log" appended. */
  get_rt_log_filename(log_filename, sizeof(log_filename), filename);
  l = rc_fopen(log_filename, 1);
  if (l == NULL)
    exit(-1);

  /* Get a lock on the log.  Probably not strictly necessary, since the table is locked
   * first, and other threads are blocked at this point... */
  if (rc_flock(l) != 0)
    fprintf(stderr, "\nError while locking log file!\n");

  /* Go to the end of the table file. */
  if (rc_fseek(f, 0, RCSEEK_END) != 0) {
    fprintf(stderr, "Error seeking to end of output file.\n");
    exit(-1);
  }

  /* If we have results that extend past the end of the file, write zeros as
   * placeholders until we get to the point where our data starts. */
  file_size = rc_ftell(f);

  rt_log(l, "Thread #%u: file size at start is %"PRIu64" (%"PRIu64" chains)\n", thread_id, file_size, file_size / CHAIN_SIZE);

  empty_chains = (int)((((start_indices[0] - file_base_chain) * CHAIN_SIZE) - file_size) / CHAIN_SIZE);

  if (empty_chains > 0)
    rt_log(l, "\tWriting %d empty chains (%u bytes)\n", empty_chains, empty_chains * CHAIN_SIZE);

  for (i = 0; i < empty_chains; i++) {
    rc_fwrite(&start, sizeof(start), 1, f);
    rc_fwrite(&start, sizeof(start), 1, f);
  }

  /* Otherwise, if another thread wrote placeholders already, seek to the point at which
   * we need to overwrite. */
  rt_log(l, "\tSeeking to position %lu (chain #%lu).\n", (start_indices[0] - file_base_chain) * CHAIN_SIZE, start_indices[0] - file_base_chain);
  if (rc_fseek(f, (start_indices[0] - file_base_chain) * CHAIN_SIZE, RCSEEK_SET) != 0) {
    perror("Error seeking in file");
    exit(-1);
  }

  /* Write the chains. */
  for (i = 0; i < start_indices_size; i++) {
    start = start_indices[i];
    for (j = (i * chains_per_work_unit); (j < ((i * chains_per_work_unit) + chains_per_work_unit)) && (j < end_indices_size); j++) {
      rc_fwrite(&start, sizeof(uint64_t), 1, f);
      rc_fwrite(&(end_indices[j]), sizeof(uint64_t), 1, f);
      start++;
    }
  }

  if (start_indices_size > 0)
    rt_log(l, "\tWrote chains start indices from %"PRIu64" to %"PRIu64"\n", start_indices[0], start - 1);

  /* Sync to disk to ensure data durability */
#ifdef _WIN32
  FlushFileBuffers(f);
#else
  fflush(f);
  fsync(fileno(f));
#endif

  /* Verify write succeeded by checking file size */
  rc_fseek(f, 0, RCSEEK_END);
  uint64_t actual_size = rc_ftell(f);
  uint64_t expected_size = (start_indices[start_indices_size - 1] - file_base_chain + 1) * CHAIN_SIZE;
  if (actual_size < expected_size) {
    fprintf(stderr, "\nWarning: file size mismatch after write. Expected at least %"PRIu64", got %"PRIu64"\n",
            expected_size, actual_size);
    rt_log(l, "\tERROR: File size mismatch. Expected %"PRIu64", got %"PRIu64"\n",
           expected_size, actual_size);
  }

  rc_fclose(l);
  rc_fclose(f);
}
