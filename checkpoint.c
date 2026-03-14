/*
 * Rainbow Crackalack: checkpoint.c
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
#else
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "checkpoint.h"
#include "file_lock.h"
#include "misc.h"


/* CRC64 polynomial (ECMA-182). */
#define CRC64_POLY 0xC96C5795D7870F42ULL

/* Build CRC64 lookup table. */
static void crc64_init_table(uint64_t *table) {
  unsigned int i, j;

  for (i = 0; i < 256; i++) {
    uint64_t crc = i;
    for (j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ CRC64_POLY;
      else
        crc >>= 1;
    }
    table[i] = crc;
  }
}

/* Compute CRC64 of buffer. */
static uint64_t crc64_compute(const void *data, size_t len) {
  static uint64_t table[256];
  static int table_initialized = 0;
  const unsigned char *p = (const unsigned char *)data;
  uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;
  size_t i;

  if (!table_initialized) {
    crc64_init_table(table);
    table_initialized = 1;
  }

  for (i = 0; i < len; i++)
    crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);

  return crc ^ 0xFFFFFFFFFFFFFFFFULL;
}

/* Get checkpoint filename from table path. */
static void get_checkpoint_filename(char *checkpoint_path, size_t size, const char *table_path) {
  snprintf(checkpoint_path, size - 1, "%s.state", table_path);
}

/* Get temporary checkpoint filename from table path. */
static void get_checkpoint_tmp_filename(char *tmp_path, size_t size, const char *table_path) {
  snprintf(tmp_path, size - 1, "%s.state.tmp", table_path);
}

/* Perform atomic file sync. */
static int atomic_sync(rc_file f) {
#ifdef _WIN32
  if (!FlushFileBuffers(f)) {
    windows_print_error("FlushFileBuffers");
    return -1;
  }
  return 0;
#else
  if (fsync(fileno(f)) != 0) {
    fprintf(stderr, "fsync failed: %s (%d)\n", strerror(errno), errno);
    return -1;
  }
  return 0;
#endif
}

/* Write checkpoint state atomically. */
static int write_checkpoint_atomic(const char *table_path, const checkpoint_state_t *state) {
  char checkpoint_path[512] = {0};
  char tmp_path[512] = {0};
  rc_file f = NULL;
  int ret = -1;
#ifndef _WIN32
  FILE *tmp_file = NULL;
#endif

  get_checkpoint_filename(checkpoint_path, sizeof(checkpoint_path), table_path);
  get_checkpoint_tmp_filename(tmp_path, sizeof(tmp_path), table_path);

#ifndef _WIN32
  /* On POSIX, create the file first since rc_fopen with append=0 uses r+ mode. */
  tmp_file = fopen(tmp_path, "w");
  if (tmp_file == NULL) {
    fprintf(stderr, "Failed to create checkpoint temp file: %s\n", tmp_path);
    return -1;
  }
  fclose(tmp_file);
#endif

  /* Write to temporary file. */
  f = rc_fopen(tmp_path, 0);
  if (f == NULL) {
    fprintf(stderr, "Failed to open checkpoint temp file: %s\n", tmp_path);
    unlink(tmp_path);
    return -1;
  }

  if (rc_fwrite(state, sizeof(checkpoint_state_t), 1, f) != 1) {
    fprintf(stderr, "Failed to write checkpoint state to temp file\n");
    rc_fclose(f);
    unlink(tmp_path);
    return -1;
  }

  /* Sync to disk before rename. */
  if (atomic_sync(f) != 0) {
    rc_fclose(f);
    unlink(tmp_path);
    return -1;
  }

  rc_fclose(f);

  /* Atomic rename. */
#ifdef _WIN32
  /* Windows requires removing target first. */
  unlink(checkpoint_path);
  if (rename(tmp_path, checkpoint_path) != 0) {
    fprintf(stderr, "Failed to rename checkpoint temp file: %s (%d)\n", strerror(errno), errno);
    unlink(tmp_path);
    return -1;
  }
#else
  if (rename(tmp_path, checkpoint_path) != 0) {
    fprintf(stderr, "Failed to rename checkpoint temp file: %s (%d)\n", strerror(errno), errno);
    unlink(tmp_path);
    return -1;
  }
#endif

  ret = 0;
  return ret;
}

/* Read checkpoint state from file. */
static int read_checkpoint(const char *table_path, checkpoint_state_t *state) {
  char checkpoint_path[512] = {0};
  rc_file f = NULL;

  get_checkpoint_filename(checkpoint_path, sizeof(checkpoint_path), table_path);

  f = rc_fopen(checkpoint_path, 0);
  if (f == NULL)
    return -1;

  if (rc_fread(state, sizeof(checkpoint_state_t), 1, f) != 1) {
    fprintf(stderr, "Failed to read checkpoint state\n");
    rc_fclose(f);
    return -1;
  }

  rc_fclose(f);
  return 0;
}

int checkpoint_init(const char *table_path, const char *device_name, uint64_t params_hash) {
  checkpoint_state_t state = {0};

  if (table_path == NULL || device_name == NULL) {
    fprintf(stderr, "checkpoint_init: NULL arguments\n");
    return -1;
  }

  state.version = CHECKPOINT_VERSION;
  state.chains_written = 0;
  state.last_start_index = 0;
  state.last_end_index = 0;
  state.checkpoint_hash = 0;
  state.timestamp = (uint64_t)time(NULL);
  state.params_hash = params_hash;

  strncpy(state.device_name, device_name, CHECKPOINT_DEVICE_NAME_LEN - 1);
  state.device_name[CHECKPOINT_DEVICE_NAME_LEN - 1] = '\0';

  return write_checkpoint_atomic(table_path, &state);
}

int checkpoint_update(const char *table_path, uint64_t chains_written,
                      uint64_t last_start, uint64_t last_end,
                      const uint64_t *recent_chains, size_t recent_count) {
  checkpoint_state_t state = {0};

  if (table_path == NULL) {
    fprintf(stderr, "checkpoint_update: NULL table_path\n");
    return -1;
  }

  /* Read existing checkpoint to preserve device_name and params_hash. */
  if (read_checkpoint(table_path, &state) != 0) {
    fprintf(stderr, "checkpoint_update: Failed to read existing checkpoint\n");
    return -1;
  }

  state.chains_written = chains_written;
  state.last_start_index = last_start;
  state.last_end_index = last_end;
  state.timestamp = (uint64_t)time(NULL);

  /* Compute CRC64 of recent chains for integrity check. */
  if (recent_chains != NULL && recent_count > 0) {
    state.checkpoint_hash = crc64_compute(recent_chains, recent_count * sizeof(uint64_t));
  } else {
    state.checkpoint_hash = 0;
  }

  return write_checkpoint_atomic(table_path, &state);
}

int checkpoint_validate(const char *table_path, checkpoint_state_t *out_state) {
  checkpoint_state_t state = {0};

  if (table_path == NULL || out_state == NULL) {
    fprintf(stderr, "checkpoint_validate: NULL arguments\n");
    return -1;
  }

  if (read_checkpoint(table_path, &state) != 0)
    return -1;

  /* Validate version. */
  if (state.version != CHECKPOINT_VERSION) {
    fprintf(stderr, "Checkpoint version mismatch: expected %u, got %u\n",
            CHECKPOINT_VERSION, state.version);
    return -1;
  }

  /* Basic sanity checks. */
  if (state.chains_written == 0) {
    fprintf(stderr, "Checkpoint has no chains written\n");
    return -1;
  }

  memcpy(out_state, &state, sizeof(checkpoint_state_t));
  return 0;
}

int checkpoint_get_resume_point(const char *table_path, uint64_t *out_start_index,
                                 uint64_t *out_chains_done, const char *current_device) {
  checkpoint_state_t state = {0};

  if (table_path == NULL || out_start_index == NULL ||
      out_chains_done == NULL || current_device == NULL) {
    fprintf(stderr, "checkpoint_get_resume_point: NULL arguments\n");
    return -1;
  }

  if (checkpoint_validate(table_path, &state) != 0)
    return -1;

  /* Warn if device changed. */
  if (strncmp(state.device_name, current_device, CHECKPOINT_DEVICE_NAME_LEN) != 0) {
    fprintf(stderr, "WARNING: Checkpoint was created on device '%s' but resuming on '%s'\n",
            state.device_name, current_device);
  }

  *out_start_index = state.last_start_index;
  *out_chains_done = state.chains_written;

  return 0;
}

void checkpoint_remove(const char *table_path) {
  char checkpoint_path[512] = {0};
  char tmp_path[512] = {0};

  if (table_path == NULL)
    return;

  get_checkpoint_filename(checkpoint_path, sizeof(checkpoint_path), table_path);
  get_checkpoint_tmp_filename(tmp_path, sizeof(tmp_path), table_path);

  unlink(checkpoint_path);
  unlink(tmp_path);
}
