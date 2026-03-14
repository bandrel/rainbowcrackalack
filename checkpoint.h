/*
 * Rainbow Crackalack: checkpoint.h
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

#ifndef _CHECKPOINT_H
#define _CHECKPOINT_H

#include <stdint.h>
#include <stddef.h>

#define CHECKPOINT_VERSION 1
#define CHECKPOINT_DEVICE_NAME_LEN 128

typedef struct {
  uint32_t version;
  uint64_t chains_written;
  uint64_t last_start_index;
  uint64_t last_end_index;
  uint64_t checkpoint_hash;
  uint64_t timestamp;
  char device_name[CHECKPOINT_DEVICE_NAME_LEN];
  uint64_t params_hash;
} checkpoint_state_t;

/* Initialize a new checkpoint file for a table. Returns 0 on success, -1 on error. */
int checkpoint_init(const char *table_path, const char *device_name, uint64_t params_hash);

/* Update checkpoint state with new chain data. Returns 0 on success, -1 on error. */
int checkpoint_update(const char *table_path, uint64_t chains_written,
                      uint64_t last_start, uint64_t last_end,
                      const uint64_t *recent_chains, size_t recent_count);

/* Validate checkpoint file and load state. Returns 0 on success, -1 on error. */
int checkpoint_validate(const char *table_path, checkpoint_state_t *out_state);

/* Get resume point from checkpoint. Returns 0 on success, -1 on error. */
int checkpoint_get_resume_point(const char *table_path, uint64_t *out_start_index,
                                 uint64_t *out_chains_done, const char *current_device);

/* Remove checkpoint file after table generation completes. */
void checkpoint_remove(const char *table_path);

#endif
