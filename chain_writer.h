/*
 * Rainbow Crackalack: chain_writer.h
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

#ifndef _CHAIN_WRITER_H
#define _CHAIN_WRITER_H

#include <stdint.h>

/* The first chain that this run will generate.  Not zero when resuming an
 * unfinished file, or when the part index is greater than zero. */
extern uint64_t first_generated_chain;

/* The chain index stored at file offset 0.  For part index N this is
 * (total_chains_in_table * N).  Resuming an unfinished table does not move the
 * start of the file, so this must NOT be set to the resume point. */
extern uint64_t file_base_chain;

/* Writes the chains given by the kernel to the file. */
void write_chains(char *filename, unsigned int chains_per_work_unit, uint64_t *start_indices, unsigned int start_indices_size, uint64_t *end_indices, unsigned int end_indices_size, unsigned int thread_id);

#endif
