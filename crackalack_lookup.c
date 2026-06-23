/*
 * Rainbow Crackalack: crackalack_lookup.c
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

/*
 * Performs GPU-accelerated password hash lookups on rainbow tables.
 */

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#define O_BINARY 0
#else
#include <sys/sysinfo.h>
#define O_BINARY 0
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <pthread.h>
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "gpu_backend.h"

#include "charset.h"
#include "gws.h"
#include "clock.h"
#include "cpu_rt_functions.h"
#include "hash_validate.h"
#include "markov.h"
#include "bloom.h"
#include "misc.h"
#include "fa_batch.h"
#include "precompute_collate.h"
#include "rtc_decompress.h"
#include "rti2_decompress.h"
#include "ppi.h"
#include "shared.h"
#include "test_shared.h"  /* TODO: move hex_to_bytes() elsewhere. */
#include "verify.h"
#include "version.h"

#define VERBOSE 1
#define PRECOMPUTE_KERNEL_PATH "precompute.cl"
#ifdef USE_METAL
#define PRECOMPUTE_NTLM8_KERNEL_PATH "precompute_ntlm8.metal"
#define PRECOMPUTE_NTLM9_KERNEL_PATH "precompute_ntlm9.metal"
#define PRECOMPUTE_NTLM10_KERNEL_PATH "precompute_ntlm10.metal"
#define PRECOMPUTE_MD5_8_KERNEL_PATH "precompute_md5_8.metal"
#define PRECOMPUTE_MD5_9_KERNEL_PATH "precompute_md5_9.metal"
#define PRECOMPUTE_MARKOV_KERNEL_PATH "precompute_markov.metal"
#define PRECOMPUTE_MARKOV_NTLM8_KERNEL_PATH "precompute_markov_ntlm8.metal"
#define PRECOMPUTE_MARKOV_NTLM9_KERNEL_PATH "precompute_markov_ntlm9.metal"
#define PRECOMPUTE_MARKOV_NTLM10_KERNEL_PATH "precompute_markov_ntlm10.metal"
#define PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH "precompute_markov_ntlm8_batch.metal"
#define PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH "precompute_ntlm8_batch.metal"
#define PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH "precompute_netntlmv1_7_batch.metal"
#elif defined(USE_CUDA)
#define PRECOMPUTE_NTLM8_KERNEL_PATH "CUDA/precompute_ntlm8.cu"
#define PRECOMPUTE_NTLM9_KERNEL_PATH "CUDA/precompute_ntlm9.cu"
#define PRECOMPUTE_NTLM10_KERNEL_PATH "CUDA/precompute_ntlm10.cu"
#define PRECOMPUTE_MD5_8_KERNEL_PATH "CUDA/precompute_md5_8.cu"
#define PRECOMPUTE_MD5_9_KERNEL_PATH "CUDA/precompute_md5_9.cu"
#define PRECOMPUTE_MARKOV_KERNEL_PATH "precompute_markov.cl"
#define PRECOMPUTE_MARKOV_NTLM8_KERNEL_PATH "CUDA/precompute_markov_ntlm8.cu"
#define PRECOMPUTE_MARKOV_NTLM9_KERNEL_PATH "CUDA/precompute_markov_ntlm9.cu"
#define PRECOMPUTE_MARKOV_NTLM10_KERNEL_PATH "CUDA/precompute_markov_ntlm10.cu"
#define PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH "CUDA/precompute_markov_ntlm8_batch.cu"
#define PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH "CUDA/precompute_ntlm8_batch.cu"
#define PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH "CUDA/precompute_netntlmv1_7_batch.cu"
#else
#define PRECOMPUTE_NTLM8_KERNEL_PATH "precompute_ntlm8.cl"
#define PRECOMPUTE_NTLM9_KERNEL_PATH "precompute_ntlm9.cl"
#define PRECOMPUTE_NTLM10_KERNEL_PATH "precompute_ntlm10.cl"
#define PRECOMPUTE_MD5_8_KERNEL_PATH "precompute_md5_8.cl"
#define PRECOMPUTE_MD5_9_KERNEL_PATH "precompute_md5_9.cl"
#define PRECOMPUTE_MARKOV_KERNEL_PATH "precompute_markov.cl"
#define PRECOMPUTE_MARKOV_NTLM8_KERNEL_PATH "precompute_markov_ntlm8.cl"
#define PRECOMPUTE_MARKOV_NTLM9_KERNEL_PATH "precompute_markov_ntlm9.cl"
#define PRECOMPUTE_MARKOV_NTLM10_KERNEL_PATH "precompute_markov_ntlm10.cl"
#define PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH "precompute_markov_ntlm8_batch.cl"
#define PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH "precompute_ntlm8_batch.cl"
#define PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH "precompute_netntlmv1_7_batch.cl"
#endif

#define FALSE_ALARM_KERNEL_PATH "false_alarm_check.cl"
#ifdef USE_METAL
#define FALSE_ALARM_NTLM10_KERNEL_PATH "false_alarm_check_ntlm10.metal"
#define FALSE_ALARM_NTLM8_KERNEL_PATH "false_alarm_check_ntlm8.metal"
#define FALSE_ALARM_NTLM9_KERNEL_PATH "false_alarm_check_ntlm9.metal"
#define FALSE_ALARM_MD5_8_KERNEL_PATH "false_alarm_check_md5_8.metal"
#define FALSE_ALARM_MD5_9_KERNEL_PATH "false_alarm_check_md5_9.metal"
#define PRECOMPUTE_NETNTLMV1_7_KERNEL_PATH "precompute_netntlmv1_7.metal"
#define FALSE_ALARM_NETNTLMV1_7_KERNEL_PATH "false_alarm_check_netntlmv1_7.metal"
#elif defined(USE_CUDA)
#define FALSE_ALARM_NTLM10_KERNEL_PATH "CUDA/false_alarm_check_ntlm10.cu"
#define FALSE_ALARM_NTLM8_KERNEL_PATH "CUDA/false_alarm_check_ntlm8.cu"
#define FALSE_ALARM_NTLM9_KERNEL_PATH "CUDA/false_alarm_check_ntlm9.cu"
#define FALSE_ALARM_MD5_8_KERNEL_PATH "CUDA/false_alarm_check_md5_8.cu"
#define FALSE_ALARM_MD5_9_KERNEL_PATH "CUDA/false_alarm_check_md5_9.cu"
#define PRECOMPUTE_NETNTLMV1_7_KERNEL_PATH "CUDA/precompute_netntlmv1_7.cu"
#define FALSE_ALARM_NETNTLMV1_7_KERNEL_PATH "CUDA/false_alarm_check_netntlmv1_7.cu"
#else
#define FALSE_ALARM_NTLM10_KERNEL_PATH "false_alarm_check_ntlm10.cl"
#define FALSE_ALARM_NTLM8_KERNEL_PATH "false_alarm_check_ntlm8.cl"
#define FALSE_ALARM_NTLM9_KERNEL_PATH "false_alarm_check_ntlm9.cl"
#define FALSE_ALARM_MD5_8_KERNEL_PATH "false_alarm_check_md5_8.cl"
#define FALSE_ALARM_MD5_9_KERNEL_PATH "false_alarm_check_md5_9.cl"
#define PRECOMPUTE_NETNTLMV1_7_KERNEL_PATH "precompute_netntlmv1_7.cl"
#define FALSE_ALARM_NETNTLMV1_7_KERNEL_PATH "false_alarm_check_netntlmv1_7.cl"
#endif
#ifdef USE_METAL
#define FALSE_ALARM_MARKOV_KERNEL_PATH "false_alarm_check_markov.metal"
#define FALSE_ALARM_MARKOV_NTLM8_KERNEL_PATH "false_alarm_check_markov_ntlm8.metal"
#define FALSE_ALARM_MARKOV_NTLM9_KERNEL_PATH "false_alarm_check_markov_ntlm9.metal"
#define FALSE_ALARM_MARKOV_NTLM10_KERNEL_PATH "false_alarm_check_markov_ntlm10.metal"
#define GPU_BINARY_SEARCH_KERNEL_PATH "gpu_binary_search.metal"
#elif defined(USE_CUDA)
#define FALSE_ALARM_MARKOV_KERNEL_PATH "false_alarm_check_markov.cl"
#define FALSE_ALARM_MARKOV_NTLM8_KERNEL_PATH "CUDA/false_alarm_check_markov_ntlm8.cu"
#define FALSE_ALARM_MARKOV_NTLM9_KERNEL_PATH "CUDA/false_alarm_check_markov_ntlm9.cu"
#define FALSE_ALARM_MARKOV_NTLM10_KERNEL_PATH "CUDA/false_alarm_check_markov_ntlm10.cu"
#define GPU_BINARY_SEARCH_KERNEL_PATH "CUDA/gpu_binary_search.cu"
#else
#define FALSE_ALARM_MARKOV_KERNEL_PATH "false_alarm_check_markov.cl"
#define FALSE_ALARM_MARKOV_NTLM8_KERNEL_PATH "false_alarm_check_markov_ntlm8.cl"
#define FALSE_ALARM_MARKOV_NTLM9_KERNEL_PATH "false_alarm_check_markov_ntlm9.cl"
#define FALSE_ALARM_MARKOV_NTLM10_KERNEL_PATH "false_alarm_check_markov_ntlm10.cl"
#define GPU_BINARY_SEARCH_KERNEL_PATH "gpu_binary_search.cl"
#endif

#define HASH_FILE_FORMAT_PLAIN 1
#define HASH_FILE_FORMAT_PWDUMP 2


/* precomputed_and_potential_indices definition lives in ppi.h. */


/* Struct to represent one GPU device. */
typedef struct {
  gpu_uint device_number;
  gpu_device device;
  gpu_context context;
  gpu_program program;
  gpu_kernel kernel;
  gpu_queue queue;
  gpu_uint num_work_units;
} gpu_dev;


/* Struct to pass arguments to a host thread. */
typedef struct {
  unsigned int hash_type;
  char *hash_name;
  char *username; /* Non-NULL when pwdump format input file given. */
  char *hash; /* In hex. */
  char *charset;
  char *charset_name;
  unsigned int plaintext_len_min;
  unsigned int plaintext_len_max;
  unsigned int table_index;
  unsigned int reduction_offset;
  unsigned int chain_len;
  unsigned char challenge[8];

  unsigned int total_devices;
  uint64_t *results;
  unsigned int num_results;

  gpu_ulong *potential_start_indices;
  unsigned int num_potential_start_indices;
  
  /* Buffer size is always num_potential_start_indices. */
  unsigned int *potential_start_index_positions;
  
  /* Length is always num_potential_start_indices. */
  gpu_ulong *hash_base_indices;

  gpu_dev gpu;

  int use_markov;
  uint64_t markov_keyspace;
  uint8_t *sorted_pos0;    /* Points into the global markov model; not owned. */
  uint8_t *sorted_bigram;  /* Points into the global markov model; not owned. */
  unsigned int markov_charset_len;
  unsigned int markov_max_positions;

  int precompute_gpu_ready;
  int false_alarm_gpu_ready;
} thread_args;


/* Per-thread search result entry. */
typedef struct {
  precomputed_and_potential_indices *ppi;
  gpu_ulong start;
  unsigned int position;
} search_result_entry;

/* Struct to pass to binary search threads. */
typedef struct {
  gpu_ulong *rainbow_table;
  uint64_t num_chains;
  bloom_filter *bf;
  precomputed_and_potential_indices *ppi_head;
  unsigned int thread_number;
  unsigned int total_threads;
  search_result_entry *local_results;
  unsigned int num_local_results;
  unsigned int local_results_capacity;
} search_thread_args;


/* State for a pipelined false alarm check (launch/harvest pattern). */
typedef struct {
  pthread_t threads[MAX_NUM_DEVICES];
  unsigned int total_devices;
  thread_args *args;
  gpu_ulong *potential_start_indices;
  unsigned int *potential_start_index_positions;
  gpu_ulong *hash_base_indices;
  precomputed_and_potential_indices **ppi_refs;
  unsigned int num_potential_start_indices;
  struct timespec start_time;
  int active;  /* 1 if GPU threads are in-flight */
} false_alarm_state;


/* Struct to hold node in linked list of preloaded tables. */
struct _preloaded_table {
  char *filepath;
  gpu_ulong *rainbow_table;
  uint64_t num_chains;
  bloom_filter *bf;
  struct _preloaded_table *next;
};
typedef struct _preloaded_table preloaded_table;


typedef struct _config_group {
  rt_parameters params;
  struct _config_group *next;
} config_group;


unsigned int count_tables(char *dir);
unsigned int count_tables_for_config(char *dir, const rt_parameters *filter);
void find_rt_params(char *dir, rt_parameters *rt_params);
void collect_config_groups(char *dir, config_group **head);
void free_config_groups(config_group **head);
precomputed_and_potential_indices *ppi_find(precomputed_and_potential_indices *head, const char *hash);
void ppi_reset_endpoints(precomputed_and_potential_indices *head);
void setup_args_for_config(thread_args *args, unsigned int num_devices, const rt_parameters *params);
void free_loaded_hashes(char **usernames, char **hashes);
void *host_thread_false_alarm(void *ptr);
void print_eta_precompute();
void rt_binary_search(gpu_ulong *rainbow_table, uint64_t num_chains, bloom_filter *bf, precomputed_and_potential_indices *ppi_head);
void gpu_binary_search_streaming(preloaded_table *pt, precomputed_and_potential_indices *ppi_head, thread_args *args, unsigned int num_devices);
void gpu_binary_search_release(void);
gpu_ulong *search_precompute_cache(char *index_data, unsigned int *num_indices, char *filename, unsigned int filename_size);
void save_precompute_cache(char *index_data, gpu_ulong *indices, unsigned int num_indices);
void search_tables(unsigned int total_tables, precomputed_and_potential_indices *ppi, thread_args *args);
void launch_false_alarm_kernel(fa_batch_t *batch, thread_args *args, false_alarm_state *state);
void harvest_false_alarm_results(false_alarm_state *state);
void save_cracked_hash(precomputed_and_potential_indices *ppi, unsigned int hash_type);


/* The path of the pot file to store cracked hashes in.  This can be overridden by
 * a command line arg. */
char jtr_pot_filename[128] = "rainbowcrackalack_jtr.pot";
char hashcat_pot_filename[128] = "rainbowcrackalack_hashcat.pot";

/* Markov mode state set by --markov flag. */
static int use_markov = 0;
static char markov_path[1024] = {0};
static markov_model g_markov = {0};

/* Aggregate bloom-filter stats across all freed tables.  Updated
 * inside the table-free paths and printed once at shutdown. */
static struct {
  uint64_t queries;
  uint64_t passes;
  uint64_t confirmed;
  unsigned int tables;
} g_bloom_agg = {0, 0, 0, 0};

/* The number of seconds spent on precomputation, file I/O, searching, and false alarm
 * checking. */
double time_precomp = 0, time_io = 0, time_searching = 0, time_falsealarms = 0;

/* The total number of false alarms, chains processed, respectively. */
uint64_t num_falsealarms = 0, num_chains_processed = 0;

/* The total number of hashes cracked in this invokation and number of tables
 * processed, respectively. */
unsigned int num_cracked = 0, num_tables_processed = 0;

/* Mutex to protect the precomputed_and_potential_indices array. _*/
pthread_mutex_t ppi_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Barrier to ensure that kernels on multiple devices are all run at the same time.
 * The closed-source AMD driver on Windows effectively blocks other devices while
 * one kernel is running; this ensures parallelization in that environment, since
 * all kernels will run at once.  The open source AMD ROCm driver on Linux may or
 * may not get a very slight performance bump with this enabled. */
pthread_barrier_t barrier = {0};

/* Set to 1 if AMD GPUs found. */
unsigned int is_amd_gpu = 0;

/* The global work size, as over-ridden by the user on the command line. */
size_t user_provided_gws = 0;

/* False-alarm batch flush threshold (candidate count).
 * 1 disables batching (single-table-per-launch behavior). */
unsigned int fa_batch_threshold = 16384;

/* Target false-positive rate for the bloom filter, set by --bloom-fpr.
 * 0 disables the bloom (bloom_create returns NULL, which the query
 * path treats as "no filter").  Default 0.01 (1%) — the entire band
 * [0.01, 0.0005] lands at m = 512 M bits / k = 11 for our typical
 * num_chains, which is twice the legacy bloom efficiency without
 * extra memory.  Tighter targets (e.g. 0.0001) land in a bigger
 * pow2 bucket and pay more in bloom-query work than they save in
 * binary-search work on this workload.  See
 * docs/superpowers/specs/2026-05-24-bloom-filter-tightening-design.md. */
double bloom_target_fpr = 0.01;

/* Set to 1 when --bloom-fpr was given an explicit value > 0, which forces the
 * bloom on (skips the auto break-even decision). */
int bloom_fpr_forced = 0;

/* Per-run decision: 1 = build per-table blooms, 0 = skip them.  Computed once in
 * streaming_lookup() before the preloader thread spawns; read by
 * load_single_table().  Defaults to 1 so any path that doesn't set it keeps the
 * historical always-build behavior. */
int g_build_bloom = 1;

/* If non-zero, search_tables() routes per-table endpoint binary search through
 * the GPU (gpu_binary_search_streaming) instead of CPU rt_binary_search.
 * Off by default while still under validation; toggle with --gpu-search. */
int gpu_search_enabled = 0;

/* Active NetNTLMv1 server challenge.  Defaults to the canonical value;
 * overridden by --challenge or adopted from the loaded tables. */
static unsigned char g_challenge[8];
static int g_challenge_set = 0;

/* The platform number to disable (-1 to not disable any). */
int disable_platform = -1;

/* The total number of precomputed indices loaded into memory.  Each one of these is
 * a gpu_ulong (8 bytes). */
uint64_t total_precomputed_indices_loaded = 0;

/* Set to 1 if the NTLM8/9 message was printed.  This prevents console spam. */
unsigned int printed_precompute_optimized_message = 0;
unsigned int printed_false_alarm_optimized_message = 0;

/* The total number of tables in all subdirectories of the directory given
 * by the user. */
unsigned int total_tables = 0;

/* Set to 1 by the preloading thread to indicate that no more tables exist for loading. */
unsigned int table_loading_complete = 0;

/* The current size of the preloaded tables list. */
unsigned int num_preloaded_tables_available = 0;

/* A linked list of preloaded tables. */
preloaded_table *preloaded_table_list = NULL;

/* Condition for the main thread to wait for more tables on. */
pthread_cond_t condition_wait_for_tables = PTHREAD_COND_INITIALIZER;

/* Condition for the preloading thread to wait on (when the MAX_PRELOAD_NUM is reached). */
pthread_cond_t condition_continue_loading_tables = PTHREAD_COND_INITIALIZER;

/* The lock for the preloaded tables system. */
pthread_mutex_t preloaded_tables_lock = PTHREAD_MUTEX_INITIALIZER;

/* The time at which precomputation begins. */
struct timespec precompute_start_time = {0};

/* The time at which table searching begins. */
struct timespec search_start_time = {0};

/* Number of uncracked hashes. */
unsigned int num_hashes = 0;

/* Number of hashes precomputed so far. */
unsigned int num_hashes_precomputed = 0;

/* Total number of hashes that will be precomputed. */
unsigned int num_hashes_precomputed_total = 0;


/* The total number of tables to preload in memory while binary searching and false
 * alarm checking is done by the main thread.  Overridable via $RCRT_MAX_PRELOAD
 * to trade RAM for throughput (each in-flight table holds ~1 GB after decompress). */
#define MAX_PRELOAD_NUM_DEFAULT 4
unsigned int max_preload_num = MAX_PRELOAD_NUM_DEFAULT;

#define LOCK_PPI() \
  if (pthread_mutex_lock(&ppi_mutex)) { perror("Failed to lock mutex"); exit(-1); }

#define UNLOCK_PPI() \
  if (pthread_mutex_unlock(&ppi_mutex)) { perror("Failed to unlock mutex"); exit(-1); }


/* Adds a potential start index (and position within the chain) to check for false
 * alarms. */
void add_potential_start_index_and_position(precomputed_and_potential_indices *ppi, gpu_ulong start, unsigned int position) {
  #define POTENTIAL_START_INDICES_INITIAL_SIZE 1024

  LOCK_PPI();

  /* Initialize the potential_start_indices buffer if it isn't already. */
  if (ppi->potential_start_indices == NULL) {
    ppi->potential_start_indices = calloc(POTENTIAL_START_INDICES_INITIAL_SIZE, sizeof(gpu_ulong));
    ppi->potential_start_index_positions = calloc(POTENTIAL_START_INDICES_INITIAL_SIZE, sizeof(unsigned int));
    if ((ppi->potential_start_indices == NULL) || (ppi->potential_start_index_positions == NULL)) {
      fprintf(stderr, "Failed to initialize potential_start_indices / potential_start_index_positions buffer.\n");
      exit(-1);
    }
    ppi->potential_start_indices_size = POTENTIAL_START_INDICES_INITIAL_SIZE;
  }

  /* If its time to re-size the array... */
  if (ppi->num_potential_start_indices == ppi->potential_start_indices_size) {
    if (ppi->potential_start_indices_size > SIZE_MAX / 2) {
      fprintf(stderr, "potential_start_indices_size overflow: cannot double beyond %zu.\n", ppi->potential_start_indices_size);
      exit(-1);
    }
    size_t new_size_in_ulongs = ppi->potential_start_indices_size * 2;

    /*printf("Resizing array from %zu to %zu.\n", ppi->potential_start_indices_size, new_size_in_ulongs);*/
    ppi->potential_start_indices = recalloc(ppi->potential_start_indices, new_size_in_ulongs * sizeof(gpu_ulong), ppi->potential_start_indices_size * sizeof(gpu_ulong));
    ppi->potential_start_index_positions = recalloc(ppi->potential_start_index_positions, new_size_in_ulongs * sizeof(unsigned int), ppi->potential_start_indices_size * sizeof(unsigned int));
    if ((ppi->potential_start_indices == NULL) || (ppi->potential_start_index_positions == NULL)) {
      fprintf(stderr, "Failed to re-allocate potential_start_indices/potential_start_index_positions buffer to %zu.\n", new_size_in_ulongs);
      exit(-1);
    }
    ppi->potential_start_indices_size = new_size_in_ulongs;
  }
  ppi->potential_start_indices[ppi->num_potential_start_indices] = start;
  ppi->potential_start_index_positions[ppi->num_potential_start_indices] = position;
  ppi->num_potential_start_indices++;

  UNLOCK_PPI();
}


/* Dispatch the false-alarm kernel for the candidates currently held in
 * `batch`.  The batch takes ownership of arrays for the duration of the
 * in-flight kernel; harvest_false_alarm_results joins the threads and
 * fa_batch_reset() should be called by the caller after harvest. */
void launch_false_alarm_kernel(fa_batch_t *batch, thread_args *args, false_alarm_state *state) {
  unsigned int total_devices = args[0].total_devices;

  state->active = 0;
  state->total_devices = total_devices;
  state->args = args;

  if (batch->num_candidates == 0) {
    printf("No matches found in batch.\n");
    return;
  }
  printf("  Checking %u potential matches (across %u table%s)...\n",
         batch->num_candidates, batch->tables_in_batch,
         batch->tables_in_batch == 1 ? "" : "s");
  fflush(stdout);
  num_falsealarms += batch->num_candidates;

  /* Sort by chain position so consecutive work items (which the kernel
   * dispatches into the same warp) have similar walk lengths.  Reduces
   * GPU warp divergence — each warp runs at the speed of its longest
   * chain, so uniform-length warps are dramatically faster. */
  fa_batch_sort_by_position(batch);

  /* Snapshot the batch arrays into state-owned copies.  The caller will
   * fa_batch_reset() immediately after this returns and start filling the
   * next iteration's candidates into the same memory -- if we let state
   * (and the worker threads) point at batch->* directly, harvest later
   * reads ppi_refs that have been overwritten by subsequent fa_batch_appends,
   * causing the hash-verify strcmp to fail and the crack to be silently
   * dropped.  Copies are freed at the end of harvest_false_alarm_results. */
  size_t n = (size_t)batch->num_candidates;
  state->potential_start_indices         = malloc(n * sizeof(*state->potential_start_indices));
  state->potential_start_index_positions = malloc(n * sizeof(*state->potential_start_index_positions));
  state->hash_base_indices               = malloc(n * sizeof(*state->hash_base_indices));
  state->ppi_refs                        = malloc(n * sizeof(*state->ppi_refs));
  if (state->potential_start_indices == NULL || state->potential_start_index_positions == NULL ||
      state->hash_base_indices == NULL || state->ppi_refs == NULL) {
    fprintf(stderr, "launch_false_alarm_kernel: malloc failed for batch snapshot (%zu candidates).\n", n);
    exit(-1);
  }
  memcpy(state->potential_start_indices,         batch->start_indices,         n * sizeof(*state->potential_start_indices));
  memcpy(state->potential_start_index_positions, batch->start_index_positions, n * sizeof(*state->potential_start_index_positions));
  memcpy(state->hash_base_indices,               batch->hash_base_indices,     n * sizeof(*state->hash_base_indices));
  memcpy(state->ppi_refs,                        batch->ppi_refs,              n * sizeof(*state->ppi_refs));
  state->num_potential_start_indices      = batch->num_candidates;

  start_timer(&state->start_time);

  for (unsigned int i = 0; i < total_devices; i++) {
    args[i].potential_start_indices         = state->potential_start_indices;
    args[i].num_potential_start_indices     = state->num_potential_start_indices;
    args[i].potential_start_index_positions = state->potential_start_index_positions;
    args[i].hash_base_indices               = state->hash_base_indices;

    if (pthread_create(&(state->threads[i]), NULL, &host_thread_false_alarm, &(args[i]))) {
      perror("Failed to create thread");
      exit(-1);
    }
  }

  state->active = 1;
}


/* Join GPU false alarm threads launched by launch_false_alarm_kernel(), process
 * results, and clear state pointers.  No-op if state->active is 0. */
void harvest_false_alarm_results(false_alarm_state *state) {
  char time_str[128] = {0};
  unsigned int i = 0, j = 0;
  double time_delta = 0.0;
  thread_args *args;
  gpu_ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1] = {0};
  int charset_len = 0;
  char chalhex[17] = {0};

  format_challenge_hex(g_challenge, chalhex);

  if (!state->active)
    return;

  args = state->args;

  /* Wait for all GPU threads to finish. */
  for (i = 0; i < state->total_devices; i++) {
    if (pthread_join(state->threads[i], NULL) != 0) {
      perror("Failed to join with thread");
      exit(-1);
    }
  }

  /* Compute charset_len for index_to_plaintext (same logic as original). */
  if (args->markov_keyspace > 0) {
    charset_len = strlen(args->charset);
    if (charset_len == 0) charset_len = 1;
    fill_plaintext_space_markov_keyspace(args->markov_keyspace, args->plaintext_len_max, plaintext_space_up_to_index);
  } else {
    if (strcmp(args->charset_name, "byte") == 0)
      charset_len = 256;
    else
      charset_len = strlen(args->charset);
    fill_plaintext_space_table(charset_len, args->plaintext_len_min, args->plaintext_len_max, plaintext_space_up_to_index);
  }

  /* Search for valid results, and update the ppi with the plaintext. */
  j = 0;
  for (i = 0; i < state->total_devices; i++) {
    unsigned int r;
    for (r = 0; r < args[i].num_results; r++, j++) {
      if (args[i].results[r] != UINT64_MAX) {
      	char plaintext[MAX_PLAINTEXT_LEN] = {0};
      	unsigned int plaintext_len = 0;
        unsigned char real_key[8] = {0};


      	if (args[i].use_markov) {
          index_to_plaintext_markov_cpu(args[i].results[r], &g_markov, args[i].plaintext_len_max, (unsigned char *)plaintext);
          plaintext_len = args[i].plaintext_len_max;
        } else
          index_to_plaintext(args[i].results[r], args[i].charset, charset_len, args[i].plaintext_len_min, args[i].plaintext_len_max, plaintext_space_up_to_index, plaintext, &plaintext_len);

      	/* Double check NTLM results to weed out super false alarms. */
      	if (args[i].hash_type == HASH_NTLM) {
      	  unsigned char hash[16] = {0};
      	  char hash_hex[(sizeof(hash) * 2) + 1] = {0};


      	  ntlm_hash(plaintext, plaintext_len, hash);
      	  if (!bytes_to_hex(hash, sizeof(hash), hash_hex, sizeof(hash_hex)) || \
      	      (strcmp(hash_hex, state->ppi_refs[j]->hash) != 0)) {
      	    continue;
      	  }
      	} else if (args[i].hash_type == HASH_MD5) {
      	  unsigned char hash[16] = {0};
      	  char hash_hex[(sizeof(hash) * 2) + 1] = {0};

      	  md5_hash(plaintext, plaintext_len, hash);
      	  if (!bytes_to_hex(hash, sizeof(hash), hash_hex, sizeof(hash_hex)) || \
      	      (strcmp(hash_hex, state->ppi_refs[j]->hash) != 0)) {
      	    continue;
      	  }
      	} else if (args[i].hash_type == HASH_NETNTLMV1) {

          unsigned char hash[8] = {0};
          char hash_hex[(sizeof(hash) * 2) + 1] = {0};
          char rkey_hex[(sizeof(hash) * 2) + 1] = {0};

          setup_des_key(plaintext, real_key);

          netntlmv1_hash(real_key, 8, hash);

          if (!bytes_to_hex(hash, sizeof(hash), hash_hex, sizeof(hash_hex)) || \
              (strncmp(hash_hex, state->ppi_refs[j]->hash, 16) != 0)) {
                bytes_to_hex(real_key, sizeof(real_key), rkey_hex, sizeof(rkey_hex));
                printf("Found super false positive!: (Net-NTLMv1('%s') == %s) != %s\n", rkey_hex, hash_hex, state->ppi_refs[j]->hash);
            continue;
          }
        } else {
      	  printf("WARNING: CPU code to double-check this cracked hash has not yet been added.  There is a 60%% chance this is a false positive!  A workaround is to use John The Ripper to validate this result(s).\n");
        }

      	/* Its official: we cracked a hash! */

        /* Skip if this ppi was already cracked by a previous match in this loop. */
        if (state->ppi_refs[j]->plaintext != NULL)
          continue;

      	/* Save the plaintext, clear the precomputed end indices list (since its
      	 * no longer useful, save the hash/plaintext combo into the pot file, and
      	 * tell the user. */
      	if (args[i].hash_type == HASH_NETNTLMV1) {
          char ptxt_hex[(7 * 2) + 1] = {0};
          bytes_to_hex((unsigned char*)plaintext, 7, ptxt_hex, sizeof(ptxt_hex));
          state->ppi_refs[j]->plaintext = strdup(ptxt_hex);
        } else {
          state->ppi_refs[j]->plaintext = strdup(plaintext);
        }
      	state->ppi_refs[j]->num_precomputed_end_indices = 0;
      	FREE(state->ppi_refs[j]->precomputed_end_indices);

      	save_cracked_hash(state->ppi_refs[j], args[i].hash_type);
        if (args[i].hash_type == HASH_NETNTLMV1) {
          printf("%sHASH CRACKED => %s:%s:%s%s\n", GREENB, state->ppi_refs[j]->hash, chalhex, state->ppi_refs[j]->plaintext, CLR);
          fflush(stdout);
        } else {
          printf("%sHASH CRACKED => %s:%s:%s%s\n", GREENB, (state->ppi_refs[j]->username != NULL) ? state->ppi_refs[j]->username : state->ppi_refs[j]->hash, chalhex, plaintext, CLR);  fflush(stdout);
        }
      }
    }
  }
  time_delta = get_elapsed(&state->start_time);

  time_falsealarms += time_delta;
  seconds_to_human_time(time_str, sizeof(time_str), (unsigned int)time_delta);
  printf("  Completed false alarm checks in %s.\n", time_str);  fflush(stdout);

  /* Free the per-launch snapshot copies allocated in launch_false_alarm_kernel.
   * (Originally these pointed straight into batch->* memory, which the caller
   * mutates between launch and harvest -- causing missed cracks.) */
  free(state->potential_start_indices);
  free(state->potential_start_index_positions);
  free(state->hash_base_indices);
  free(state->ppi_refs);
  state->potential_start_indices         = NULL;
  state->potential_start_index_positions = NULL;
  state->hash_base_indices               = NULL;
  state->ppi_refs                        = NULL;
  for (i = 0; i < state->total_devices; i++) {
    FREE(args[i].results);
    args[i].num_results = 0;
  }
  state->active = 0;
}


/* Print a warning to the user if a lot of memory is used by the pre-computed indices. */
void check_memory_usage() {
  uint64_t total_memory = get_total_memory(), num_precompute_bytes = 0;
  double percent_memory_used = 0.0;


  if (total_memory == 0)
    return;

  num_precompute_bytes = total_precomputed_indices_loaded * sizeof(gpu_ulong);
  percent_memory_used = ((double)num_precompute_bytes / (double)total_memory) * 100;
  if (percent_memory_used > 65) {
    printf("\n\n\n\t!! WARNING !!\n\n\tThe pre-computed indices take up more than 65%% of total RAM!  This may result in strange failures from clFinish() and other OpenCL functions.  If this happens, either run this lookup with a smaller number of hashes at a time, or do it on a machine with more memory.\n\n\tMemory used by pre-compute indices: %"QUOTE PRIu64"\n\tTotal RAM: %"QUOTE PRIu64"\n\tPercent used: %.1f%%\n\n\n\n", num_precompute_bytes, total_memory, percent_memory_used);
  }
}


/* Free all the potential start indices. */
void clear_potential_start_indices(precomputed_and_potential_indices *ppi) {
  precomputed_and_potential_indices *ppi_cur = ppi;


  while(ppi_cur) {
    FREE(ppi_cur->potential_start_indices);
    FREE(ppi_cur->potential_start_index_positions);
    ppi_cur->num_potential_start_indices = 0;

    ppi_cur = ppi_cur->next;
  }
}


/* Returns the total number of *.rt and *.rtc in all subdirectories of the
 * specified directory. */
unsigned int count_tables(char *dir) {
  DIR *d = NULL;
  struct dirent *de = NULL;
  unsigned int ret = 0, is_file = 0, is_dir = 0;


  d = opendir(dir);
  if (d == NULL) {
    fprintf(stderr, "Failed to open directory %s: %s\n", dir, strerror(errno)); fflush(stderr);
    return 0;
  }

  while ((de = readdir(d)) != NULL) {
#ifdef _WIN32
    struct stat st = {0};
    char path[256] = {0};

    /* The d_type field of the dirent struct is not a POSIX standard, and Windows
     * doesn't support it.  So we fall back to using stat(). */
    snprintf(path, sizeof(path) - 1, "%s\\%s", dir, de->d_name);
    if (stat(path, &st) < 0) {
      fprintf(stderr, "Error: failed to stat() %s: %s.  Continuing anyway...\n", path, strerror(errno));  fflush(stderr);
      is_file = 0;
      is_dir = 0;
    } else {
      is_file = S_ISREG(st.st_mode);
      is_dir = S_ISDIR(st.st_mode);
    }
#else
    /* Linux has the d_type field, which is much more efficient to use than doing
     * another stat(). */
    is_file = (de->d_type == DT_REG) || (de->d_type == DT_LNK);
    is_dir = (de->d_type == DT_DIR);
#endif

    if (is_file && (str_ends_with(de->d_name, ".rt") || str_ends_with(de->d_name, ".rtc")))
      ret++;
    else if (is_dir && (strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)) {
      char subdir_path[1024] = {0};
      filepath_join(subdir_path, sizeof(subdir_path) - 1, dir, de->d_name);
      ret += count_tables(subdir_path);
    }
  }

  closedir(d);
  return ret;
}


/* Free the hashes we loaded from disk or command line. */
void free_loaded_hashes(char **usernames, char **hashes) {
  unsigned int i = 0;

  if (usernames != NULL) {
    for (i = 0; i < num_hashes; i++) {
      FREE(usernames[i]);
    }
    FREE(usernames);
  }

  if (hashes != NULL) {
    for (i = 0; i < num_hashes; i++) {
      FREE(hashes[i]);
    }
    FREE(hashes);
  }
  num_hashes = 0;
}


/* Recursively searches the target directory for the first rainbow table file, and uses its filename to infer
 * the rainbow table parameters. */
void find_rt_params(char *dir_name, rt_parameters *rt_params) {
  char filepath[512] = {0};
  DIR *dir = NULL;
  struct dirent *de = NULL;
  struct stat st;


  dir = opendir(dir_name);
  if (dir == NULL)  /* This directory may not allow the current process permission. */
    return;

  while ((de = readdir(dir)) != NULL) {

    /* Create an absolute path to this entity. */
    filepath_join(filepath, sizeof(filepath), dir_name, de->d_name);

    /* If this is a directory, recurse into it. */
    if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0) && (stat(filepath, &st) == 0) && S_ISDIR(st.st_mode)) {
      find_rt_params(filepath, rt_params);

      /* If we're searching for rainbowtable parameters, and successfully parsed them
       * in the recursive call, we're done. */
      if ((rt_params != NULL) && rt_params->parsed) {
	closedir(dir); dir = NULL;
	return;
      }

    /* If this is a compressed or uncompressed rainbow table, process it! */
    } else if (str_ends_with(de->d_name, ".rt") || str_ends_with(de->d_name, ".rtc") || str_ends_with(de->d_name, ".rti2")) {

      /* Try to parse them from this file name.  On success, return immediately
       * (no further processing needed), otherwise continue searching until the
       * first valid set of parameters is found. */
      parse_rt_params(rt_params, filepath);
      if (rt_params->parsed) {
	closedir(dir); dir = NULL;
	return;
      }

    }
  }

  closedir(dir); dir = NULL;
}


/* Returns 1 if two rt_parameters describe the same table configuration
 * (same precomputed-endpoint requirements), otherwise 0. */
static int configs_match(const rt_parameters *a, const rt_parameters *b) {
  return a->hash_type == b->hash_type
      && strcmp(a->charset_name, b->charset_name) == 0
      && a->plaintext_len_min == b->plaintext_len_min
      && a->plaintext_len_max == b->plaintext_len_max
      && a->table_index == b->table_index
      && a->chain_len == b->chain_len
      && a->markov_keyspace == b->markov_keyspace
      /* The NetNTLMv1 challenge is part of a table's identity: tables built for
       * different challenges are not interchangeable, so they must not share a
       * config group.  Keeping them separate also lets the challenge-resolution
       * scan in main() detect a directory holding conflicting challenges. */
      && memcmp(a->challenge, b->challenge, NETNTLMV1_CHALLENGE_LEN) == 0;
}


static void collect_config_groups_dir(char *dir_name, config_group **head) {
  char filepath[512] = {0};
  DIR *dir = NULL;
  struct dirent *de = NULL;
  struct stat st;

  dir = opendir(dir_name);
  if (dir == NULL)
    return;

  while ((de = readdir(dir)) != NULL) {
    filepath_join(filepath, sizeof(filepath), dir_name, de->d_name);

    if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)
        && (stat(filepath, &st) == 0) && S_ISDIR(st.st_mode)) {
      collect_config_groups_dir(filepath, head);

    } else if (str_ends_with(de->d_name, ".rt") || str_ends_with(de->d_name, ".rtc") || str_ends_with(de->d_name, ".rti2")) {
      rt_parameters p = {0};
      parse_rt_params(&p, filepath);
      if (!p.parsed)
        continue;

      /* Add this config only if not already present. */
      int found = 0;
      for (config_group *cg = *head; cg != NULL; cg = cg->next) {
        if (configs_match(&cg->params, &p)) { found = 1; break; }
      }
      if (!found) {
        config_group *cg = calloc(1, sizeof(config_group));
        if (cg == NULL) { fprintf(stderr, "OOM in collect_config_groups\n"); exit(-1); }
        cg->params = p;
        cg->next = *head;
        *head = cg;
      }
    }
  }

  closedir(dir);
}

void collect_config_groups(char *dir, config_group **head) {
  *head = NULL;
  collect_config_groups_dir(dir, head);
}

void free_config_groups(config_group **head) {
  config_group *cg = *head, *next = NULL;
  while (cg != NULL) {
    next = cg->next;
    FREE(cg);
    cg = next;
  }
  *head = NULL;
}

/* Count only tables whose parsed params match filter. */
unsigned int count_tables_for_config(char *dir, const rt_parameters *filter) {
  unsigned int count = 0;
  DIR *d = opendir(dir);
  if (d == NULL)
    return 0;

  struct dirent *de = NULL;
  while ((de = readdir(d)) != NULL) {
    char filepath[512] = {0};
    struct stat st = {0};
    filepath_join(filepath, sizeof(filepath), dir, de->d_name);

    if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0)
        && (stat(filepath, &st) == 0) && S_ISDIR(st.st_mode)) {
      count += count_tables_for_config(filepath, filter);
    } else if (str_ends_with(de->d_name, ".rt") || str_ends_with(de->d_name, ".rtc") || str_ends_with(de->d_name, ".rti2")) {
      rt_parameters p = {0};
      parse_rt_params(&p, filepath);
      if (p.parsed && configs_match(&p, filter))
        count++;
    }
  }

  closedir(d);
  return count;
}

/* Find a ppi node by hash string. */
precomputed_and_potential_indices *ppi_find(precomputed_and_potential_indices *head, const char *hash) {
  while (head != NULL) {
    if (head->hash != NULL && strcmp(head->hash, hash) == 0)
      return head;
    head = head->next;
  }
  return NULL;
}

/* For each uncracked ppi node, free precomputed endpoints so they can be
 * recomputed for a different table configuration. */
void ppi_reset_endpoints(precomputed_and_potential_indices *head) {
  while (head != NULL) {
    if (head->plaintext == NULL) {
      FREE(head->precomputed_end_indices);
      head->num_precomputed_end_indices = 0;

    }
    head = head->next;
  }
}

/* Update thread args with the given table configuration. */
void setup_args_for_config(thread_args *args, unsigned int num_devices, const rt_parameters *params) {
  for (unsigned int idx = 0; idx < num_devices; idx++) {
    args[idx].hash_type         = params->hash_type;
    args[idx].hash_name         = (char *)params->hash_name;
    args[idx].charset           = validate_charset((char *)params->charset_name);
    args[idx].charset_name      = (char *)params->charset_name;
    args[idx].plaintext_len_min = params->plaintext_len_min;
    args[idx].plaintext_len_max = params->plaintext_len_max;
    args[idx].table_index       = params->table_index;
    args[idx].reduction_offset  = params->reduction_offset;
    args[idx].chain_len         = params->chain_len;
    args[idx].markov_keyspace   = params->markov_keyspace;
    memcpy(args[idx].challenge, g_challenge, 8);
  }
}


/* Free the precomputed_hashes linked list. */
void free_precomputed_and_potential_indices(precomputed_and_potential_indices **ppi_head) {
  precomputed_and_potential_indices *ppi = *ppi_head, *ppi_next = NULL;


  while (ppi) {
    ppi_next = ppi->next;

    FREE(ppi->precomputed_end_indices);
    FREE(ppi->potential_start_indices);
    FREE(ppi->potential_start_index_positions);

    ppi->num_potential_start_indices = 0;
    FREE(ppi->plaintext);
    FREE(ppi);

    ppi = ppi_next;
  }
  *ppi_head = NULL;
}


/* Returns the number of CPU cores on this machine. */
unsigned int get_num_cpu_cores() {
#ifdef _WIN32
  SYSTEM_INFO sysinfo = {0};

  GetSystemInfo(&sysinfo);
  return sysinfo.dwNumberOfProcessors;
#elif defined(__APPLE__)
  int count = 0;
  size_t len = sizeof(count);
  sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0);
  return (unsigned int)count;
#else
  return get_nprocs();
#endif
}


/* A host thread which controls each GPU for false alarm checks. */
void *host_thread_false_alarm(void *ptr) {
  /* Push the global CUDA context onto this thread before any CUDA call.
   * On non-CUDA backends this expands to a no-op macro. */
  gpu_thread_attach();

  thread_args *args = (thread_args *)ptr;
  gpu_dev *gpu = &(args->gpu);
  gpu_context context = NULL;
  gpu_queue queue = NULL;
  gpu_kernel kernel = NULL;
  int err = 0;
  char *kernel_path = FALSE_ALARM_KERNEL_PATH, *kernel_name = "false_alarm_check";

  gpu_buffer hash_type_buffer = NULL, charset_buffer = NULL, charset_len_buffer = NULL, plaintext_len_min_buffer = NULL, plaintext_len_max_buffer = NULL, reduction_offset_buffer = NULL, plaintext_space_total_buffer = NULL, plaintext_space_up_to_index_buffer = NULL, device_num_buffer = NULL, total_devices_buffer = NULL, num_start_indices_buffer = NULL, start_indices_buffer = NULL, start_index_positions_buffer = NULL, hash_base_indices_buffer = NULL, output_block_buffer = NULL, exec_block_scaler_buffer = NULL;
  gpu_buffer sorted_pos0_buffer = NULL, sorted_bigram_buffer = NULL, max_positions_buffer = NULL;
  gpu_buffer challenge_buffer = NULL;
  /*gpu_buffer debug_ulong_buffer = NULL;*/

  gpu_ulong *start_indices = NULL, *hash_base_indices = NULL, *plaintext_indices = NULL, *output_block = NULL;
  unsigned int *start_index_positions = NULL;

  unsigned int num_start_indices = 0, num_start_index_positions = 0, num_hash_base_indices = 0, num_plaintext_indices = 0, num_exec_blocks = 0, output_block_len = 0, exec_block = 0, output_block_index = 0, plaintext_indicies_index = 0;
  uint64_t plaintext_space_total = 0;
  gpu_ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1] = {0};
  size_t gws = 0, kernel_work_group_size = 0, kernel_preferred_work_group_size_multiple = 0;
  /*gpu_ulong debug_ulong[128] = {0};*/
  int charset_len = 0;
  if (args->markov_keyspace > 0) {
    charset_len = strlen(args->charset);
    if (charset_len == 0) charset_len = 1;
    plaintext_space_total = fill_plaintext_space_markov_keyspace(args->markov_keyspace, args->plaintext_len_max, plaintext_space_up_to_index);
  } else {
    if (strcmp(args->charset_name, "byte") == 0) {
      charset_len = 256;
    } else {
      charset_len = strlen(args->charset);
    }
    plaintext_space_total = fill_plaintext_space_table(charset_len, args->plaintext_len_min, args->plaintext_len_max, plaintext_space_up_to_index);
  }

  num_start_indices = num_start_index_positions = num_hash_base_indices = num_plaintext_indices = args->num_potential_start_indices;

  /* Defensive: if no start indices were supplied, there's nothing to check.
   * Returning early prevents a divide-by-zero at `num_start_indices / gws`
   * further down (gws is derived from num_start_indices). */
  if (num_start_indices == 0) {
    args->results = NULL;
    args->num_results = 0;
    pthread_exit(NULL);
    return NULL;
  }

  start_indices = args->potential_start_indices;
  start_index_positions = args->potential_start_index_positions;
  hash_base_indices = args->hash_base_indices;

  plaintext_indices = malloc(num_plaintext_indices * sizeof(gpu_ulong));
  if (plaintext_indices == NULL) {
    fprintf(stderr, "Error while allocating buffers.\n");
    exit(-1);
  }
  memset(plaintext_indices, 0xFF, num_plaintext_indices * sizeof(gpu_ulong));

  /* If we're generating the standard NTLM 8-character tables, use the special
   * optimized kernel instead! */
  if (is_ntlm8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len)) {
    kernel_path = FALSE_ALARM_NTLM8_KERNEL_PATH;
    kernel_name = "false_alarm_check_ntlm8";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized NTLM8 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  } else if (is_ntlm9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len)) {
    kernel_path = FALSE_ALARM_NTLM9_KERNEL_PATH;
    kernel_name = "false_alarm_check_ntlm9";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized NTLM9 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  } else if (is_ntlm10(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = FALSE_ALARM_NTLM10_KERNEL_PATH;
    kernel_name = "false_alarm_check_ntlm10";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) {
      printf("\nNote: optimized NTLM10 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  } else if (is_netntlmv1_7(args->hash_type, args->charset_name, args->plaintext_len_min, args->plaintext_len_max, args->chain_len)) {
    kernel_path = FALSE_ALARM_NETNTLMV1_7_KERNEL_PATH;
    kernel_name = "false_alarm_check_netntlmv1_7";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) {
      printf("\nNote: optimized NetNTLMv1-7 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  } else if (is_md5_8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = FALSE_ALARM_MD5_8_KERNEL_PATH;
    kernel_name = "false_alarm_check_md5_8";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized MD5_8 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  } else if (is_md5_9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = FALSE_ALARM_MD5_9_KERNEL_PATH;
    kernel_name = "false_alarm_check_md5_9";
    if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized MD5_9 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
      printed_false_alarm_optimized_message = 1;
    }
  }

  /* When --markov is active, override with the Markov false alarm kernel.
   * Use optimized Markov fast-path kernels for NTLM8/NTLM9 when parameters match. */
  if (args->use_markov) {
    if (is_markov_ntlm8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len, args->use_markov)) {
      kernel_path = FALSE_ALARM_MARKOV_NTLM8_KERNEL_PATH;
      kernel_name = "false_alarm_check_markov_ntlm8";
      if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM8 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
        printed_false_alarm_optimized_message = 1;
      }
    } else if (is_markov_ntlm9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len, args->use_markov)) {
      kernel_path = FALSE_ALARM_MARKOV_NTLM9_KERNEL_PATH;
      kernel_name = "false_alarm_check_markov_ntlm9";
      if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM9 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
        printed_false_alarm_optimized_message = 1;
      }
    } else if (is_markov_ntlm10(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->use_markov)) {
      kernel_path = FALSE_ALARM_MARKOV_NTLM10_KERNEL_PATH;
      kernel_name = "false_alarm_check_markov_ntlm10";
      if ((args->gpu.device_number == 0) && (printed_false_alarm_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM10 kernel will be used for false alarm checks.\n\n"); fflush(stdout);
        printed_false_alarm_optimized_message = 1;
      }
    } else {
      kernel_path = FALSE_ALARM_MARKOV_KERNEL_PATH;
      kernel_name = "false_alarm_check_markov";
    }
  }

  /* Compile kernel once and reuse across table iterations. */
  if (!args->false_alarm_gpu_ready) {
    gpu->context = CLCREATECONTEXT(context_callback, &(gpu->device));
    gpu->queue = CLCREATEQUEUE(gpu->context, gpu->device);
    load_kernel(gpu->context, 1, &(gpu->device), kernel_path, kernel_name, &(gpu->program), &(gpu->kernel), args->hash_type);
    args->false_alarm_gpu_ready = 1;
  }

  /* These variables are set so the CLCREATEARG* macros work correctly. */
  context = gpu->context;
  queue = gpu->queue;
  kernel = gpu->kernel;

#if defined(USE_METAL) || defined(USE_CUDA)
  if ((gpu_get_kernel_work_group_info(kernel, gpu->device, GPU_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &kernel_work_group_size) != GPU_SUCCESS) || \
      (gpu_get_kernel_work_group_info(kernel, gpu->device, GPU_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &kernel_preferred_work_group_size_multiple) != GPU_SUCCESS)) {
#else
  if ((rc_clGetKernelWorkGroupInfo(kernel, gpu->device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &kernel_work_group_size, NULL) != CL_SUCCESS) || \
      (rc_clGetKernelWorkGroupInfo(kernel, gpu->device, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &kernel_preferred_work_group_size_multiple, NULL) != CL_SUCCESS)) {
#endif
    fprintf(stderr, "Failed to get preferred work group size!\n");
    CLRELEASEKERNEL(gpu->kernel);
    CLRELEASEPROGRAM(gpu->program);
    CLRELEASEQUEUE(gpu->queue);
    CLRELEASECONTEXT(gpu->context);
    args->false_alarm_gpu_ready = 0;
    pthread_exit(NULL);
    return NULL;
  }

  /* If the user provided a static GWS on the command line, use that.   Otherwise,
   * use the driver's work group size multiplied by the preferred multiple. */
  if (user_provided_gws > 0) {
    gws = user_provided_gws;
    printf("GPU #%u is using user-provided GWS value of %"PRIu64"\n", gpu->device_number, (uint64_t)gws);
  } else {
    /*gws = kernel_work_group_size * kernel_preferred_work_group_size_multiple;*/

    /* NOTE: false alarm kernels write to g_plaintext_indices[index_pos] where index_pos
     * is an absolute position (not block-local like precompute.cl's get_global_id(0)).
     * When GWS < num_start_indices and the kernel runs in multiple blocks, index_pos
     * can fall outside [0, gws), causing OOB writes. Until the kernels are fixed to
     * write block-locally, GWS must equal num_start_indices (single dispatch). */
    gws = num_start_indices;

    /* Somehow, on AMD GPUs, the kernel crashes with a message like:
     *
     *   Memory access fault by GPU node-2 (Agent handle: 0x1bb5e00) on address
     *   0x7f4c80b27000. Reason: Page not present or supervisor privilege.
     *
     * A work-around is to set the GWS to the number of start indices and just do it in
     * one pass. */
    if (is_amd_gpu)
      gws = num_start_indices;

    /*printf("GPU #%u is using dynamic GWS: %"PRIu64" (work group) x %"PRIu64" (pref. multiple) = %"PRIu64"\n", gpu->device_number, kernel_work_group_size, kernel_preferred_work_group_size_multiple, gws);*/
  }
  fflush(stdout);


  /* Count the number of times we need to run the kernel. */
  num_exec_blocks = num_start_indices / gws;
  if (num_start_indices % gws != 0)
    num_exec_blocks++;
  //printf("num_exec_blocks: %d, num_start_indices: %d\n", num_exec_blocks, num_start_indices);

  /* Output buffer must be at least num_start_indices to prevent OOB writes,
   * since false alarm kernels write at absolute index_pos positions. */
  output_block_len = (gws > num_start_indices) ? gws : num_start_indices;
  output_block = malloc(output_block_len * sizeof(gpu_ulong));
  if (output_block == NULL) {
    fprintf(stderr, "Error while allocating output buffer(s).\n");
    exit(-1);
  }
  memset(output_block, 0xFF, output_block_len * sizeof(gpu_ulong));

  CLCREATEARG(0, hash_type_buffer, CL_RO, args->hash_type, sizeof(gpu_uint));
  CLCREATEARG_ARRAY(1, charset_buffer, CL_RO, args->charset, charset_len);
  CLCREATEARG(2, charset_len_buffer, CL_RO, charset_len, sizeof(gpu_uint));
  CLCREATEARG(3, plaintext_len_min_buffer, CL_RO, args->plaintext_len_min, sizeof(gpu_uint));
  CLCREATEARG(4, plaintext_len_max_buffer, CL_RO, args->plaintext_len_max, sizeof(gpu_uint));
  CLCREATEARG(5, reduction_offset_buffer, CL_RO, args->reduction_offset, sizeof(gpu_uint));
  CLCREATEARG(6, plaintext_space_total_buffer, CL_RO, plaintext_space_total, sizeof(gpu_ulong));
  CLCREATEARG_ARRAY(7, plaintext_space_up_to_index_buffer, CL_RO, plaintext_space_up_to_index, MAX_PLAINTEXT_LEN * sizeof(gpu_ulong));
  CLCREATEARG(8, device_num_buffer, CL_RO, gpu->device_number, sizeof(gpu_uint));
  CLCREATEARG(9, total_devices_buffer, CL_RO, args->total_devices, sizeof(gpu_uint));
  CLCREATEARG(10, num_start_indices_buffer, CL_RO, num_start_indices, sizeof(gpu_uint));
  CLCREATEARG_ARRAY(11, start_indices_buffer, CL_RO, start_indices, num_start_indices * sizeof(gpu_ulong));
  CLCREATEARG_ARRAY(12, start_index_positions_buffer, CL_RO, start_index_positions, num_start_index_positions * sizeof(unsigned int));
  CLCREATEARG_ARRAY(13, hash_base_indices_buffer, CL_RO, hash_base_indices, num_hash_base_indices * sizeof(gpu_ulong));
  CLCREATEARG_ARRAY(15, output_block_buffer, CL_WO, output_block, output_block_len * sizeof(gpu_ulong));
  if (args->use_markov) {
    CLCREATEARG_ARRAY(16, sorted_pos0_buffer, CL_RO, args->sorted_pos0, args->markov_charset_len * sizeof(uint8_t));
    CLCREATEARG_ARRAY(17, sorted_bigram_buffer, CL_RO, args->sorted_bigram, args->markov_max_positions * args->markov_charset_len * args->markov_charset_len * sizeof(uint8_t));
    CLCREATEARG(18, max_positions_buffer, CL_RO, args->markov_max_positions, sizeof(gpu_uint));
  }

  if (is_netntlmv1_7(args->hash_type, args->charset_name, args->plaintext_len_min, args->plaintext_len_max, args->chain_len)) {
    CLCREATEARG_ARRAY(16, challenge_buffer, CL_RO, args->challenge, NETNTLMV1_CHALLENGE_LEN);
  }

  for (exec_block = 0; exec_block < num_exec_blocks; exec_block++) {
    unsigned int exec_block_scaler = exec_block * gws;

    CLCREATEARG(14, exec_block_scaler_buffer, CL_RO, exec_block_scaler, sizeof(gpu_uint));

    if (is_amd_gpu) {
      int barrier_ret = pthread_barrier_wait(&barrier);
      if ((barrier_ret != 0) && (barrier_ret != PTHREAD_BARRIER_SERIAL_THREAD)) {
	fprintf(stderr, "pthread_barrier_wait() failed!\n"); fflush(stderr);
	exit(-1);
      }
    }

    /* Run the kernel and wait for it to finish. */
    CLRUNKERNEL(gpu->queue, gpu->kernel, &gws);
    CLFLUSH(gpu->queue);
    CLWAIT(gpu->queue);

    /* Read the results. */
    CLREADBUFFER(output_block_buffer, output_block_len * sizeof(gpu_ulong), output_block);

    output_block_index = 0;
    while ((plaintext_indicies_index < num_plaintext_indices) && (output_block_index < output_block_len))
      plaintext_indices[plaintext_indicies_index++] = output_block[output_block_index++];

    CLFREEBUFFER(exec_block_scaler_buffer);
  }

  /* Set the results so the main thread can access them. */
  args->results = plaintext_indices;
  args->num_results = num_plaintext_indices;  

  FREE(output_block);

  CLFREEBUFFER(hash_type_buffer);
  CLFREEBUFFER(charset_buffer);
  CLFREEBUFFER(plaintext_len_min_buffer);
  CLFREEBUFFER(plaintext_len_max_buffer);
  CLFREEBUFFER(reduction_offset_buffer);
  CLFREEBUFFER(plaintext_space_total_buffer);
  CLFREEBUFFER(plaintext_space_up_to_index_buffer);
  CLFREEBUFFER(device_num_buffer);
  CLFREEBUFFER(total_devices_buffer);
  CLFREEBUFFER(num_start_indices_buffer);
  CLFREEBUFFER(start_indices_buffer);
  CLFREEBUFFER(start_index_positions_buffer);
  CLFREEBUFFER(hash_base_indices_buffer);
  CLFREEBUFFER(output_block_buffer);
  CLFREEBUFFER(challenge_buffer);
  if (args->use_markov) {
    CLFREEBUFFER(sorted_pos0_buffer);
    CLFREEBUFFER(sorted_bigram_buffer);
    CLFREEBUFFER(max_positions_buffer);
  }

  /* Context/program/kernel/queue are kept alive for reuse across tables.
   * They are released by release_false_alarm_gpu(). */

  gpu_thread_detach();
  pthread_exit(NULL);
  return NULL;
}


static void release_false_alarm_gpu(unsigned int num_devices, thread_args *args) {
  unsigned int i;
  for (i = 0; i < num_devices; i++) {
    if (args[i].false_alarm_gpu_ready) {
      CLRELEASEKERNEL(args[i].gpu.kernel);
      CLRELEASEPROGRAM(args[i].gpu.program);
      CLRELEASEQUEUE(args[i].gpu.queue);
      CLRELEASECONTEXT(args[i].gpu.context);
      args[i].false_alarm_gpu_ready = 0;
    }
  }
}


/* A host thread which controls each GPU for hash pre-computation. */
void *host_thread_precompute(void *ptr) {
  /* Push the global CUDA context onto this thread before any CUDA call.
   * On non-CUDA backends this expands to a no-op macro. */
  gpu_thread_attach();

  thread_args *args = (thread_args *)ptr;
  gpu_dev *gpu = &(args->gpu);
  gpu_context context = NULL;
  gpu_queue queue = NULL;
  gpu_kernel kernel = NULL;
  int err = 0;
  char *kernel_path = PRECOMPUTE_KERNEL_PATH, *kernel_name = "precompute";

  gpu_buffer hash_type_buffer = NULL, hash_buffer = NULL, hash_len_buffer = NULL, charset_buffer = NULL, charset_len_buffer = NULL, plaintext_len_min_buffer = NULL, plaintext_len_max_buffer = NULL, table_index_buffer = NULL, chain_len_buffer = NULL, device_num_buffer = NULL, total_devices_buffer = NULL, exec_block_scaler_buffer = NULL, output_block_buffer = NULL, pspace_table_buffer = NULL, pspace_total_buffer = NULL, sorted_pos0_buffer = NULL, sorted_bigram_buffer = NULL, max_positions_buffer = NULL/*, debug_buffer = NULL*/;
  gpu_buffer challenge_buffer = NULL;

  size_t gws = 0;
  gpu_ulong *output = NULL, *output_block = NULL;
  unsigned int output_len = 0, output_block_len = 0, num_exec_blocks = 0, exec_block = 0, output_index = 0, output_block_index = 0;
  /*unsigned int i = 0;*/

  unsigned char hash_binary[32] = {0};
  gpu_uint hash_binary_len = 0;


  /* Convert the hash from a hex string to bytes.*/
  hash_binary_len = hex_to_bytes(args->hash, sizeof(hash_binary), hash_binary);

  /* The work size is the chain length divided among the total number of GPUs.  Round
   * up if it doesn't divide evenly; this results in slightly more work being done in
   * order to get complete coverage. */
  output_len = args->chain_len / args->total_devices;
  if ((args->chain_len % args->total_devices) != 0)
    output_len++;

  /* If we're generating the standard NTLM 8-character tables, use the special
   * optimized kernel instead! */
  if (is_ntlm8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len)) {
    kernel_path = PRECOMPUTE_NTLM8_KERNEL_PATH;
    kernel_name = "precompute_ntlm8";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized NTLM8 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  } else if (is_ntlm9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len)) {
    kernel_path = PRECOMPUTE_NTLM9_KERNEL_PATH;
    kernel_name = "precompute_ntlm9";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized NTLM9 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  } else if (is_ntlm10(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = PRECOMPUTE_NTLM10_KERNEL_PATH;
    kernel_name = "precompute_ntlm10";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) {
      printf("\nNote: optimized NTLM10 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  } else if (is_netntlmv1_7(args->hash_type, args->charset_name, args->plaintext_len_min, args->plaintext_len_max, args->chain_len)) {
    kernel_path = PRECOMPUTE_NETNTLMV1_7_KERNEL_PATH;
    kernel_name = "precompute_netntlmv1_7";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) {
      printf("\nNote: optimized NetNTLMv1-7 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  } else if (is_md5_8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = PRECOMPUTE_MD5_8_KERNEL_PATH;
    kernel_name = "precompute_md5_8";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized MD5_8 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  } else if (is_md5_9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max)) {
    kernel_path = PRECOMPUTE_MD5_9_KERNEL_PATH;
    kernel_name = "precompute_md5_9";
    if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) { /* Only the first thread prints this, and only prints it once. */
      printf("\nNote: optimized MD5_9 kernel will be used for precomputation.\n\n"); fflush(stdout);
      printed_precompute_optimized_message = 1;
    }
  }

  /* When --markov is active, override with the Markov precompute kernel.
   * Use optimized Markov fast-path kernels for NTLM8/NTLM9 when parameters match. */
  if (args->use_markov) {
    if (is_markov_ntlm8(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len, args->use_markov)) {
      kernel_path = PRECOMPUTE_MARKOV_NTLM8_KERNEL_PATH;
      kernel_name = "precompute_markov_ntlm8";
      if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM8 kernel will be used for precomputation.\n\n"); fflush(stdout);
        printed_precompute_optimized_message = 1;
      }
    } else if (is_markov_ntlm9(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->reduction_offset, args->chain_len, args->use_markov)) {
      kernel_path = PRECOMPUTE_MARKOV_NTLM9_KERNEL_PATH;
      kernel_name = "precompute_markov_ntlm9";
      if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM9 kernel will be used for precomputation.\n\n"); fflush(stdout);
        printed_precompute_optimized_message = 1;
      }
    } else if (is_markov_ntlm10(args->hash_type, args->charset, args->plaintext_len_min, args->plaintext_len_max, args->use_markov)) {
      kernel_path = PRECOMPUTE_MARKOV_NTLM10_KERNEL_PATH;
      kernel_name = "precompute_markov_ntlm10";
      if ((args->gpu.device_number == 0) && (printed_precompute_optimized_message == 0)) {
        printf("\nNote: optimized Markov NTLM10 kernel will be used for precomputation.\n\n"); fflush(stdout);
        printed_precompute_optimized_message = 1;
      }
    } else {
      kernel_path = PRECOMPUTE_MARKOV_KERNEL_PATH;
      kernel_name = "precompute_markov";
    }
  }

  /* Compile kernel once and reuse across invocations. */
  if (!args->precompute_gpu_ready) {
    gpu->context = CLCREATECONTEXT(context_callback, &(gpu->device));
    gpu->queue = CLCREATEQUEUE(gpu->context, gpu->device);
    load_kernel(gpu->context, 1, &(gpu->device), kernel_path, kernel_name, &(gpu->program), &(gpu->kernel), args->hash_type);
    args->precompute_gpu_ready = 1;
  }

  /* These variables are set so the CLCREATEARG* macros work correctly. */
  context = gpu->context;
  queue = gpu->queue;
  kernel = gpu->kernel;

#if defined(USE_METAL) || defined(USE_CUDA)
  if (gpu_get_kernel_work_group_info(kernel, gpu->device, GPU_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &gws) != GPU_SUCCESS) {
#else
  if (rc_clGetKernelWorkGroupInfo(kernel, gpu->device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &gws, NULL) != CL_SUCCESS) {
#endif
    fprintf(stderr, "Failed to get preferred work group size!\n");
    CLRELEASEKERNEL(gpu->kernel);
    CLRELEASEPROGRAM(gpu->program);
    CLRELEASEQUEUE(gpu->queue);
    CLRELEASECONTEXT(gpu->context);
    args->precompute_gpu_ready = 0;
    pthread_exit(NULL);
    return NULL;
  }
  if (user_provided_gws > 0) {
    gws = user_provided_gws;
    printf("GPU #%u precompute using user-provided GWS: %"PRIu64"\n", gpu->device_number, (uint64_t)gws);
  } else if (get_optimal_gws(gpu->device, kernel_name) > 0) {
    gws = get_optimal_gws(gpu->device, kernel_name);
    printf("GPU #%u precompute using optimized GWS: %"PRIu64"\n", gpu->device_number, (uint64_t)gws);
  } else {
    gws = gws * gpu->num_work_units;
  }
  fflush(stdout);

  /* In the event that the global work size is larger than the number of outputs we
   * need, cap the GWS. */
  if (gws > output_len) gws = output_len;

  /* Count the number of times we need to run the kernel. */
  num_exec_blocks = output_len / gws;
  if (output_len % gws != 0)
    num_exec_blocks++;

  /*printf("Host thread #%u started; GWS: %zu.\n", gpu->device_number, gws);*/

  /* This will hold the results from this one GPU. */
  output = calloc(output_len, sizeof(gpu_ulong));

  /* Holds the results from one kernel exec. */
  output_block_len = gws;
  output_block = calloc(output_block_len, sizeof(gpu_ulong));

  if ((output == NULL) || (output_block == NULL)) {
    fprintf(stderr, "Error while allocating output buffer(s).\n");
    exit(-1);
  }

  /* Get the number of compute units in this device. */
  /*get_device_uint(gpu->device, CL_DEVICE_MAX_COMPUTE_UNITS, &(gpu->num_work_units));*/

  int charset_len = 0;
  if (strcmp(args->charset_name, "byte") == 0) {
    charset_len = 256;
  } else {
    charset_len = strlen(args->charset);
  }


  gpu_ulong chain_len_ulong = args->chain_len;

  CLCREATEARG(0, hash_type_buffer, CL_RO, args->hash_type, sizeof(gpu_uint));
  CLCREATEARG_ARRAY(1, hash_buffer, CL_RO, hash_binary, hash_binary_len);
  CLCREATEARG(2, hash_len_buffer, CL_RO, hash_binary_len, sizeof(gpu_uint));
  CLCREATEARG_ARRAY(3, charset_buffer, CL_RO, args->charset, charset_len);
  CLCREATEARG(4, charset_len_buffer, CL_RO, charset_len, sizeof(gpu_uint));
  CLCREATEARG(5, plaintext_len_min_buffer, CL_RO, args->plaintext_len_min, sizeof(gpu_uint));
  CLCREATEARG(6, plaintext_len_max_buffer, CL_RO, args->plaintext_len_max, sizeof(gpu_uint));
  CLCREATEARG(7, table_index_buffer, CL_RO, args->table_index, sizeof(gpu_uint));
  CLCREATEARG(8, chain_len_buffer, CL_RO, chain_len_ulong, sizeof(gpu_ulong));
  CLCREATEARG(9, device_num_buffer, CL_RO, gpu->device_number, sizeof(gpu_uint));
  CLCREATEARG(10, total_devices_buffer, CL_RO, args->total_devices, sizeof(gpu_uint));
  CLCREATEARG_ARRAY(12, output_block_buffer, CL_WO, output_block, output_block_len * sizeof(gpu_ulong));

  {
    uint64_t pspace_up_to_index[MAX_PLAINTEXT_LEN + 1] = {0};
    gpu_ulong pspace_total;
    if (args->markov_keyspace > 0)
      pspace_total = fill_plaintext_space_markov_keyspace(args->markov_keyspace, args->plaintext_len_max, pspace_up_to_index);
    else {
      pspace_total = fill_plaintext_space_table(charset_len, args->plaintext_len_min, args->plaintext_len_max, pspace_up_to_index);
    }
    CLCREATEARG_ARRAY(13, pspace_table_buffer, CL_RO, pspace_up_to_index, MAX_PLAINTEXT_LEN * sizeof(gpu_ulong));
    CLCREATEARG(14, pspace_total_buffer, CL_RO, pspace_total, sizeof(gpu_ulong));
    if (args->use_markov) {
      CLCREATEARG_ARRAY(15, sorted_pos0_buffer, CL_RO, args->sorted_pos0, args->markov_charset_len * sizeof(uint8_t));
      CLCREATEARG_ARRAY(16, sorted_bigram_buffer, CL_RO, args->sorted_bigram, args->markov_max_positions * args->markov_charset_len * args->markov_charset_len * sizeof(uint8_t));
      CLCREATEARG(17, max_positions_buffer, CL_RO, args->markov_max_positions, sizeof(gpu_uint));
    }
  }

  if (is_netntlmv1_7(args->hash_type, args->charset_name, args->plaintext_len_min, args->plaintext_len_max, args->chain_len)) {
    CLCREATEARG_ARRAY(15, challenge_buffer, CL_RO, args->challenge, NETNTLMV1_CHALLENGE_LEN);
  }

  for (exec_block = 0; exec_block < num_exec_blocks; exec_block++) {
    unsigned int exec_block_scaler = exec_block * gws;


    CLCREATEARG(11, exec_block_scaler_buffer, CL_RO, exec_block_scaler, sizeof(gpu_uint));

    if (is_amd_gpu) {
      int barrier_ret = pthread_barrier_wait(&barrier);
      if ((barrier_ret != 0) && (barrier_ret != PTHREAD_BARRIER_SERIAL_THREAD)) {
	fprintf(stderr, "pthread_barrier_wait() failed!\n"); fflush(stderr);
	exit(-1);
      }
    }

    /* Run the kernel and wait for it to finish. */
    CLRUNKERNEL(gpu->queue, gpu->kernel, &gws);
    CLFLUSH(gpu->queue);
    CLWAIT(gpu->queue);

    /* Read the results. */
    CLREADBUFFER(output_block_buffer, output_block_len * sizeof(gpu_ulong), output_block);

    /* Append this block out output to the total output for this GPU. */
    output_block_index = 0;
    while ((output_index < output_len) && (output_block_index < output_block_len))
      output[output_index++] = output_block[output_block_index++];

    CLFREEBUFFER(exec_block_scaler_buffer);
  }

  /* Set the results so the main thread can access them. */
  args->results = output;
  args->num_results = output_len;

  /*
  printf("GPU %u: ", gpu->device_number);
  for (unsigned int i = 0; i < output_len; i++) {
    printf("%"PRIu64" ", output[i]);
  }
  printf("\n");
  */

  FREE(output_block);

  CLFREEBUFFER(hash_type_buffer);
  CLFREEBUFFER(hash_buffer);
  CLFREEBUFFER(hash_len_buffer);
  CLFREEBUFFER(charset_buffer);
  CLFREEBUFFER(plaintext_len_min_buffer);
  CLFREEBUFFER(plaintext_len_max_buffer);
  CLFREEBUFFER(table_index_buffer);
  CLFREEBUFFER(chain_len_buffer);
  CLFREEBUFFER(device_num_buffer);
  CLFREEBUFFER(total_devices_buffer);
  CLFREEBUFFER(exec_block_scaler_buffer);
  CLFREEBUFFER(output_block_buffer);
  CLFREEBUFFER(pspace_table_buffer);
  CLFREEBUFFER(pspace_total_buffer);
  CLFREEBUFFER(challenge_buffer);
  if (args->use_markov) {
    CLFREEBUFFER(sorted_pos0_buffer);
    CLFREEBUFFER(sorted_bigram_buffer);
    CLFREEBUFFER(max_positions_buffer);
  }

  /* Context/program/kernel/queue are kept alive for reuse across hashes.
   * They are released by release_precompute_gpu(). */

  gpu_thread_detach();
  pthread_exit(NULL);
  return NULL;
}


static void release_precompute_gpu(unsigned int num_devices, thread_args *args) {
  unsigned int i;
  for (i = 0; i < num_devices; i++) {
    if (args[i].precompute_gpu_ready) {
      CLRELEASEKERNEL(args[i].gpu.kernel);
      CLRELEASEPROGRAM(args[i].gpu.program);
      CLRELEASEQUEUE(args[i].gpu.queue);
      CLRELEASECONTEXT(args[i].gpu.context);
      args[i].precompute_gpu_ready = 0;
    }
  }
}


/* Builds the index_data cache key for a single hash configuration.  The
 * format is fixed for interchange with blurbdust's rcracki.precalc cache:
 *
 *   {hash_name}_{charset_name}#{plaintext_len_min}-{plaintext_len_max}_
 *     {table_index}_{chain_len}:{hash}\n
 *
 * (e.g. "ntlm_loweralpha#8-8_0_100:49e5bfaab1be72a6c5236f15736a3e15\n")
 *
 * `buf` is filled in.  The trailing newline is part of the key — match
 * blurbdust exactly so caches are interchangeable. */
static void build_precompute_index_data(char *buf, size_t buf_size,
                                        const thread_args *args,
                                        const char *hash) {
  char cs[128];
  build_precompute_cache_charset(cs, sizeof(cs), args->charset_name, args->challenge);
  snprintf(buf, buf_size - 1, "%s_%s#%u-%u_%u_%u:%s\n",
           args->hash_name, cs,
           args->plaintext_len_min, args->plaintext_len_max,
           args->table_index, args->chain_len, hash);
}


/* Scans *.index files in the current working directory for a key matching
 * index_data.  On hit, opens the corresponding rcracki.precalc.N file
 * (the .index filename with the ".index" suffix removed) and reads it as
 * a contiguous array of gpu_ulong.  Returns a calloc'd array (caller frees),
 * with *num_indices set to the array length and filename[] set to the .index
 * file's name.  Returns NULL on miss.  Exits on I/O errors mid-scan, matching
 * the blurbdust upstream behaviour. */
gpu_ulong *search_precompute_cache(char *index_data, unsigned int *num_indices, char *filename, unsigned int filename_size) {
  char buf[256] = {0};
  int64_t file_size = 0;
  DIR *d = NULL;
  struct dirent *de = NULL;
  FILE *f = NULL;
  gpu_ulong *ret = NULL;

  *num_indices = 0;
  memset(filename, 0, filename_size);

  /* Go through all *.index files in the current directory and find any that match
   * the hash passed to us.  If found, we already pre-computed the values. */
  d = opendir(".");
  if (d == NULL) {
    fprintf(stderr, "Can't open current directory.\n");
    exit(-1);
  }
  while ((de = readdir(d)) != NULL) {
    if (!str_ends_with(de->d_name, ".index"))
      continue;

    /* Open this *.index file. */
    f = fopen(de->d_name, "rb");
    if (f == NULL) {
      fprintf(stderr, "Failed to open %s for reading.\n", de->d_name);
      exit(-1);
    }

    file_size = get_file_size(f);

    /* Read the index data. */
    if ((file_size <= 0) || ((size_t)file_size >= sizeof(buf))) {
      FCLOSE(f);
      continue;
    }
    memset(buf, 0, sizeof(buf));
    if (fread(buf, sizeof(char), file_size, f) != (size_t)file_size) {
      fprintf(stderr, "Failed to read index data: %s\n", strerror(errno));
      exit(-1);
    }

    FCLOSE(f);

    /* We found an index file that matches all our parameters.  Open its
     * related file containing precomputed indices. */
    if (strcmp(index_data, buf) == 0) {
      /* Set the filename to the *.index file for the caller. */
      strncpy(filename, de->d_name, filename_size - 1);

      /* Strip the trailing ".index" (6 chars) to get the data filename. */
      de->d_name[strlen(de->d_name) - 6] = '\0';

      f = fopen(de->d_name, "rb");
      if (f == NULL) {
        fprintf(stderr, "Failed to open precomputed index file: %s\n", de->d_name);
        exit(-1);
      }

      file_size = get_file_size(f);

      if (file_size % sizeof(gpu_ulong) != 0) {
        fprintf(stderr, "Precomputed indices file is not a multiple of %"PRIu64": %"PRId64"\n",
                (uint64_t)sizeof(gpu_ulong), file_size);
        exit(-1);
      }

      *num_indices = file_size / sizeof(gpu_ulong);

      ret = calloc(*num_indices, sizeof(gpu_ulong));
      if (ret == NULL) {
        fprintf(stderr, "Failed to create indices buffer.\n");
        exit(-1);
      }

      if (fread(ret, sizeof(gpu_ulong), *num_indices, f) != *num_indices) {
        fprintf(stderr, "Failed to read indices file.\n");
        exit(-1);
      }
      FCLOSE(f);

      break;
    }
  }
  closedir(d);
  d = NULL;
  return ret;
}


/* Writes a new precompute cache entry for index_data + indices[0..num_indices].
 *
 * Atomically creates the lowest-numbered free rcracki.precalc.N file via
 * O_CREAT|O_EXCL (so concurrent runs don't clobber each other), writes the
 * indices array verbatim, then writes the matching rcracki.precalc.N.index
 * file containing the raw index_data bytes (no extra newline beyond what's
 * already in index_data).
 *
 * On-disk format matches blurbdust exactly.  Exits on error. */
void save_precompute_cache(char *index_data, gpu_ulong *indices, unsigned int num_indices) {
  char filename[128] = {0};
  FILE *f = NULL;
  unsigned int i = 0;

  /* Search for the first unused filename in the space of rcracki.precalc.[0-1048576]. */
  for (i = 0; i < 1048576; i++) {
    int fd = -1;

    snprintf(filename, sizeof(filename) - 1, "rcracki.precalc.%u", i);

    /* Create a file for writing with permissions of 0600.  O_EXCL guarantees
     * we don't trample a concurrent writer. */
    fd = open(filename, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR);

    if (fd != -1) {  /* On success, convert to a file pointer. */
      f = fdopen(fd, "wb");
      break;
    }
  }

  if (f == NULL) {
    fprintf(stderr, "Error: could not create any precalc file (rcracki.precalc.[0-1048576])\n");
    exit(-1);
  }

  if (fwrite(indices, sizeof(gpu_ulong), num_indices, f) != num_indices) {
    fprintf(stderr, "Error writing precalc file: %s\n", strerror(errno));
    FCLOSE(f);
    exit(-1);
  }

  FCLOSE(f);

  /* Now create the matching rcracki.precalc.N.index file. */
  strncat(filename, ".index", sizeof(filename) - strlen(filename) - 1);
  f = fopen(filename, "wb");
  if (f == NULL) {
    fprintf(stderr, "Error while creating file: %s\n", filename);
    exit(-1);
  }
  if (fwrite(index_data, sizeof(char), strlen(index_data), f) != strlen(index_data)) {
    fprintf(stderr, "Error writing precalc index file: %s\n", strerror(errno));
    FCLOSE(f);
    exit(-1);
  }
  FCLOSE(f);
}


/* CPU precompute: walks the chain from every position for a single hash,
 * producing a ppi node with all candidate endpoints.  Mirrors what the GPU
 * batch kernel does, but runs on the CPU using cpu_rt_functions primitives.
 * Returns a heap-allocated ppi node, or NULL on error. */
/* Batched precompute: processes ALL hashes in a single GPU kernel dispatch.
 * Instead of dispatching one kernel per hash (sequential, ~4.7s each), this
 * sends all hashes to the GPU at once, achieving near-constant time regardless
 * of hash count (up to GPU memory limits).
 *
 * Supports both standard and Markov NTLM8 tables.
 *
 * If index_data_array is non-NULL, each hash's unfiltered output is also
 * written to a disk cache entry (rcracki.precalc.N + .index file) before
 * the non-zero filter is applied.  This makes a second lookup of the same
 * hashes a cache hit (precompute is skipped entirely).
 *
 * Returns 1 if batched path was used, 0 if it should fall back to per-hash. */
int batch_precompute_all_hashes(unsigned int num_devices, thread_args *args,
    char **hashes, char **usernames, unsigned int num_hashes,
    char **index_data_array,
    precomputed_and_potential_indices **ppi_head) {

  int use_markov_batch = args[0].use_markov &&
      is_markov_ntlm8(args[0].hash_type, args[0].charset, args[0].plaintext_len_min,
        args[0].plaintext_len_max, args[0].reduction_offset, args[0].chain_len, args[0].use_markov);
  int use_standard_batch = !args[0].use_markov &&
      is_ntlm8(args[0].hash_type, args[0].charset, args[0].plaintext_len_min,
        args[0].plaintext_len_max, args[0].reduction_offset, args[0].chain_len);
  int use_netntlmv1_batch = is_netntlmv1_7(args[0].hash_type, args[0].charset_name,
        args[0].plaintext_len_min, args[0].plaintext_len_max, args[0].chain_len);

  if (!use_markov_batch && !use_standard_batch && !use_netntlmv1_batch)
    return 0;
  if (num_hashes < 2)
    return 0;

  gpu_dev *gpu = &(args[0].gpu);
  int err = 0;
  gpu_context context = NULL;
  gpu_queue queue = NULL;
  gpu_kernel kernel = NULL;
  struct timespec batch_start = {0};

  gpu_buffer hashes_buffer = NULL, num_hashes_buffer = NULL, positions_buffer = NULL;
  gpu_buffer charset_len_buffer = NULL, chain_len_buffer = NULL;
  gpu_buffer device_num_buffer = NULL, total_devices_buffer = NULL;
  gpu_buffer output_buffer = NULL;
  gpu_buffer sorted_pos0_buffer = NULL, sorted_bigram_buffer = NULL;
  gpu_buffer challenge_buffer = NULL;

  unsigned int positions_per_hash = args[0].chain_len;  /* Single device: all positions */
  size_t total_work_items = (size_t)num_hashes * positions_per_hash;

  const char *batch_label = use_markov_batch ? "Markov NTLM8" : use_netntlmv1_batch ? "NetNTLMv1-7" : "NTLM8";
  printf("\n  Batched precompute (%s): %u hashes x %u positions = %zu work items\n",
         batch_label,
         num_hashes, positions_per_hash, total_work_items);
  fflush(stdout);

  /* Check output buffer fits in GPU memory. Each entry is 8 bytes. */
  size_t output_bytes = total_work_items * sizeof(gpu_ulong);
  gpu_ulong gpu_mem = 0;
  get_device_ulong(gpu->device, CL_DEVICE_GLOBAL_MEM_SIZE, &gpu_mem);
  if (output_bytes > gpu_mem / 2) {
    printf("  Output buffer too large (%.0f MB vs %.0f MB GPU mem). Falling back to per-hash.\n",
           (double)output_bytes / 1048576.0, (double)gpu_mem / 1048576.0);
    return 0;
  }

  /* Convert all hashes to binary and concatenate. */
  unsigned char *all_hashes_bin = calloc(num_hashes, 16);
  if (all_hashes_bin == NULL) {
    fprintf(stderr, "Error allocating batch hash buffer.\n");
    return 0;
  }
  for (unsigned int i = 0; i < num_hashes; i++)
    hex_to_bytes(hashes[i], 16, all_hashes_bin + i * 16);

  /* Allocate output buffer on host. */
  gpu_ulong *all_output = calloc(total_work_items, sizeof(gpu_ulong));
  if (all_output == NULL) {
    fprintf(stderr, "Error allocating batch output buffer.\n");
    free(all_hashes_bin);
    return 0;
  }

  /* Set up GPU context and load the appropriate batch kernel. */
  gpu->context = CLCREATECONTEXT(context_callback, &(gpu->device));
  gpu->queue = CLCREATEQUEUE(gpu->context, gpu->device);
  if (use_markov_batch) {
    load_kernel(gpu->context, 1, &(gpu->device),
                PRECOMPUTE_MARKOV_NTLM8_BATCH_KERNEL_PATH,
                "precompute_markov_ntlm8_batch",
                &(gpu->program), &(gpu->kernel), args[0].hash_type);
  } else if (use_netntlmv1_batch) {
    load_kernel(gpu->context, 1, &(gpu->device),
                PRECOMPUTE_NETNTLMV1_7_BATCH_KERNEL_PATH,
                "precompute_netntlmv1_7_batch",
                &(gpu->program), &(gpu->kernel), args[0].hash_type);
  } else {
    load_kernel(gpu->context, 1, &(gpu->device),
                PRECOMPUTE_NTLM8_BATCH_KERNEL_PATH,
                "precompute_ntlm8_batch",
                &(gpu->program), &(gpu->kernel), args[0].hash_type);
  }

  context = gpu->context;
  queue = gpu->queue;
  kernel = gpu->kernel;

  gpu_ulong chain_len_ulong = args[0].chain_len;
  gpu_uint device_num = 0;
  gpu_uint num_hashes_uint = num_hashes;
  gpu_uint positions_uint = positions_per_hash;

  /* Set kernel arguments.  Args 0-2 and 4-7 are shared across all batch
   * kernels.  Arg 3 differs: charset_len for NTLM, reduction_offset for
   * NetNTLMv1. */
  CLCREATEARG_ARRAY(0, hashes_buffer, CL_RO, all_hashes_bin, num_hashes * 16);
  CLCREATEARG(1, num_hashes_buffer, CL_RO, num_hashes_uint, sizeof(gpu_uint));
  CLCREATEARG(2, positions_buffer, CL_RO, positions_uint, sizeof(gpu_uint));

  if (use_netntlmv1_batch) {
    gpu_uint reduction_offset = args[0].reduction_offset;
    CLCREATEARG(3, charset_len_buffer, CL_RO, reduction_offset, sizeof(gpu_uint));
  } else {
    int charset_len = strlen(args[0].charset);
    CLCREATEARG(3, charset_len_buffer, CL_RO, charset_len, sizeof(gpu_uint));
  }

  CLCREATEARG(4, chain_len_buffer, CL_RO, chain_len_ulong, sizeof(gpu_ulong));
  CLCREATEARG(5, device_num_buffer, CL_RO, device_num, sizeof(gpu_uint));  /* pos_start, updated per chunk */
  CLCREATEARG(6, total_devices_buffer, CL_RO, positions_uint, sizeof(gpu_uint));  /* total_positions */
  CLCREATEARG_ARRAY(7, output_buffer, CL_WO, all_output, output_bytes);

  /* Markov batch kernel takes two additional args for the Markov statistics. */
  if (use_markov_batch) {
    CLCREATEARG_ARRAY(8, sorted_pos0_buffer, CL_RO, args[0].sorted_pos0,
                      args[0].markov_charset_len * sizeof(uint8_t));
    CLCREATEARG_ARRAY(9, sorted_bigram_buffer, CL_RO, args[0].sorted_bigram,
                      args[0].markov_max_positions * args[0].markov_charset_len *
                      args[0].markov_charset_len * sizeof(uint8_t));
  }

  /* NetNTLMv1-7 batch kernel takes the server challenge at index 8. */
  if (use_netntlmv1_batch) {
    CLCREATEARG_ARRAY(8, challenge_buffer, CL_RO, args[0].challenge, NETNTLMV1_CHALLENGE_LEN);
  }

  /* Dispatch in position-based sub-batches to stay within GPU watchdog limits.
   * Each sub-batch processes all hashes at a range of chain positions.
   *
   * Wall time scales roughly as chain_len^2 / (2 * chunk_size) when the
   * dispatch has too few work-items to saturate the GPU.  On a 3080 Ti +
   * 2 NetNTLMv1-7 hashes (chain_len 881689) we measured:
   *
   *     chunk_size  work-items/chunk  precompute  speedup vs 512
   *            512             1024     63 min          1x
   *           2048             4096     16 min       ~3.9x
   *           4096             8192      8 min       ~7.6x
   *           8192            16384      4 min        15x  <- new default
   *
   * 8192 saturates the SM slots even for very small hash counts.  At this
   * size the slowest chunk takes ~5 sec, comfortably below NVIDIA's default
   * 30 sec TDR.  Honor $RCRT_PRECOMP_CHUNK to retune per-GPU without
   * recompiling (e.g. larger hash batches may benefit from a smaller chunk
   * because the hash dimension already saturates the GPU). */
  unsigned int chunk_size = compute_batch_chunk_size(num_hashes);
  const char *cs_env = getenv("RCRT_PRECOMP_CHUNK");
  if (cs_env != NULL && *cs_env != '\0') {
    int n = atoi(cs_env);
    if (n >= 32 && n <= 65536) chunk_size = (unsigned int)n;
  }
  if (chunk_size > positions_per_hash) chunk_size = positions_per_hash;
  unsigned int num_chunks = (positions_per_hash + chunk_size - 1) / chunk_size;

  printf("  Dispatching batch kernel: %u hashes in %u position chunks of %u...\n",
         num_hashes, num_chunks, chunk_size);
  fflush(stdout);
  start_timer(&batch_start);

  /* We need to tell the kernel which position range to process.
   * Reuse device_num as position offset and total_devices as chunk size. */
  for (unsigned int chunk = 0; chunk < num_chunks; chunk++) {
    unsigned int pos_start = chunk * chunk_size;
    unsigned int pos_end = pos_start + chunk_size;
    if (pos_end > positions_per_hash) pos_end = positions_per_hash;
    unsigned int chunk_positions = pos_end - pos_start;

    /* Update device_num to serve as position offset for this chunk. */
    gpu_uint pos_start_val = pos_start;
    CLWRITEBUFFER(device_num_buffer, sizeof(gpu_uint), &pos_start_val);
    gpu_uint chunk_pos_val = chunk_positions;
    CLWRITEBUFFER(positions_buffer, sizeof(gpu_uint), &chunk_pos_val);

    size_t chunk_gws = (size_t)num_hashes * chunk_positions;
    chunk_gws = ((chunk_gws + 255) / 256) * 256;

    CLRUNKERNEL(gpu->queue, gpu->kernel, &chunk_gws);
    CLFLUSH(gpu->queue);
    CLWAIT(gpu->queue);
  }

  char time_str[128] = {0};
  seconds_to_human_time(time_str, sizeof(time_str), get_elapsed(&batch_start));
  printf("  Batch precompute completed in %s for all %u hashes.\n", time_str, num_hashes);
  fflush(stdout);

  /* Read results back. */
  CLREADBUFFER(output_buffer, output_bytes, all_output);

  /* DEBUG: optional raw output dump for diagnosing batch correctness.
   * Set RCRT_DEBUG_DUMP_PRECOMP=/path/to/file to dump all_output verbatim
   * (num_hashes x positions_per_hash gpu_ulong entries). */
  {
    const char *dump_path = getenv("RCRT_DEBUG_DUMP_PRECOMP");
    if (dump_path != NULL && *dump_path != '\0') {
      FILE *df = fopen(dump_path, "wb");
      if (df != NULL) {
        if (fwrite(all_output, sizeof(gpu_ulong), (size_t)num_hashes * positions_per_hash, df) != (size_t)num_hashes * positions_per_hash) {
          fprintf(stderr, "  RCRT_DEBUG_DUMP_PRECOMP: short write to %s\n", dump_path);
        } else {
          printf("  RCRT_DEBUG_DUMP_PRECOMP: dumped %zu bytes to %s\n",
                 (size_t)num_hashes * positions_per_hash * sizeof(gpu_ulong), dump_path);
        }
        fclose(df);
        fflush(stdout);
      } else {
        fprintf(stderr, "  RCRT_DEBUG_DUMP_PRECOMP: could not open %s for write\n", dump_path);
      }
    }
  }

  /* Distribute results to per-hash ppi nodes. */
  for (unsigned int h = 0; h < num_hashes; h++) {
    gpu_ulong *hash_output = all_output + (size_t)h * positions_per_hash;

    /* Sanity check: all-zero precompute means the kernel didn't run for this
     * hash (or the hash truly maps to no plaintexts at every position, which
     * is vanishingly unlikely for a realistic config).  Match blurbdust's
     * fail-fast behaviour. */
    unsigned int p = 0;
    for (p = 0; p < positions_per_hash; p++)
      if (hash_output[p] != 0) break;
    if (p == positions_per_hash) {
      fprintf(stderr, "Error: all zeros in precomputation!\n");
      exit(-1);
    }

    /* Write the unfiltered output to the disk cache before we apply the
     * non-zero filter below.  blurbdust's on-disk format is the full
     * chain_len-1 array including zeros, so the cache is interchangeable
     * with rcracki and with the pre-refactor crackalack_lookup. */
    if (index_data_array != NULL && index_data_array[h] != NULL)
      save_precompute_cache(index_data_array[h], hash_output, positions_per_hash);

    /* Create ppi node. */
    precomputed_and_potential_indices *ppi = calloc(1, sizeof(precomputed_and_potential_indices));
    if (ppi == NULL) { fprintf(stderr, "Error allocating ppi.\n"); exit(-1); }

    ppi->username = usernames[h];
    ppi->hash = hashes[h];

    /* Batched-precompute reverse-order fix (all batched hash types: ntlm8 /
     * netntlmv1_7 / markov_ntlm8).
     *
     * The search records a matched endpoint's POSITION as its index in
     * precomputed_end_indices, and the false-alarm kernel reconstructs the
     * chain to exactly that depth (pos < endpoint+1) -- so the array must be
     * indexed by chain column.  The single-hash (non-batch) path reverse-
     * collates its GPU results to achieve this, but the batched path wrote
     * hash_output[p] straight through, and the batch kernels compute the
     * endpoint for chain column (positions_per_hash - 2 - p) at output index p
     * -- i.e. REVERSE column order.  Uncorrected, the false-alarm walk targets
     * the mirrored depth and every batched (>=2-hash) crack is silently
     * dropped (a +constant shift cannot fix it -- the index is mirrored, not
     * merely offset).  Rebuild the array in column order:
     *   precomputed_end_indices[C] = hash_output[positions_per_hash - 2 - C].
     * Slots with no source (the last column, and the trailing zeroed position)
     * hold a UINT64_MAX sentinel that never matches a real endpoint (endpoints
     * are < the plaintext space). */
    ppi->num_precomputed_end_indices = positions_per_hash;
    ppi->precomputed_end_indices = malloc((size_t)positions_per_hash * sizeof(gpu_ulong));
    if (ppi->precomputed_end_indices == NULL) { fprintf(stderr, "Error allocating ppi indices.\n"); exit(-1); }
    collate_batched_precompute_endpoints((const uint64_t *)hash_output,
                                         positions_per_hash,
                                         (uint64_t *)ppi->precomputed_end_indices);

    /* Append to linked list. */
    if (*ppi_head == NULL) {
      *ppi_head = ppi;
    } else {
      precomputed_and_potential_indices *tail = *ppi_head;
      while (tail->next != NULL)
        tail = tail->next;
      tail->next = ppi;
    }
  }

  /* Cleanup GPU resources. */
  CLFREEBUFFER(hashes_buffer);
  CLFREEBUFFER(num_hashes_buffer);
  CLFREEBUFFER(positions_buffer);
  CLFREEBUFFER(charset_len_buffer);
  CLFREEBUFFER(chain_len_buffer);
  CLFREEBUFFER(device_num_buffer);
  CLFREEBUFFER(total_devices_buffer);
  CLFREEBUFFER(output_buffer);
  if (use_markov_batch) {
    CLFREEBUFFER(sorted_pos0_buffer);
    CLFREEBUFFER(sorted_bigram_buffer);
  }
  CLFREEBUFFER(challenge_buffer);
  CLRELEASEKERNEL(gpu->kernel);
  CLRELEASEPROGRAM(gpu->program);
  CLRELEASEQUEUE(gpu->queue);
  CLRELEASECONTEXT(gpu->context);

  free(all_hashes_bin);
  free(all_output);

  return 1;
}


/* If update_ppi is non-NULL, replace its precomputed endpoints in-place (for
 * re-running precomputation against a different table configuration).
 * If NULL, a new ppi node is appended to ppi_head (original behaviour).
 *
 * If index_data is non-NULL, the computed indices are written to a disk
 * cache entry (rcracki.precalc.N + .index file) so a subsequent run hits
 * the cache instead of recomputing. */
void precompute_hash(unsigned int num_devices, thread_args *args, precomputed_and_potential_indices **ppi_head, precomputed_and_potential_indices *update_ppi, char *index_data) {
  pthread_t threads[MAX_NUM_DEVICES] = {0};
  char time_str[128] = {0};
  struct timespec start_time = {0};
  unsigned int i = 0, j = 0, output_index = 0;
  int k = 0;
  uint64_t *output = NULL;
  precomputed_and_potential_indices *ppi = NULL;

  /* Start the timer for this hash. */
  start_timer(&start_time);

  /* Start one thread to control each GPU. */
  for (i = 0; i < num_devices; i++) {
    if (pthread_create(&(threads[i]), NULL, &host_thread_precompute, &(args[i]))) {
      perror("Failed to create thread");
      exit(-1);
    }
  }

  /* Wait for all threads to finish. */
  for (i = 0; i < num_devices; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      perror("Failed to join with thread");
      exit(-1);
    }
  }

  num_hashes_precomputed++;

  seconds_to_human_time(time_str, sizeof(time_str), get_elapsed(&start_time));
  printf("  Completed in %s.\n", time_str);  fflush(stdout);
  print_eta_precompute();

  /* Create one output array to hold all the results. */
  output = calloc(args[0].num_results * num_devices, sizeof(uint64_t));
  if (output == NULL) {
    fprintf(stderr, "Error allocating buffer for GPU results.\n");
    exit(-1);
  }

  /*
    The results end up spread out like this across many GPUs:

    GPU 0: 100 94 88 82 76 70 64 58 52 46 40 34 28 22 16 10 4
    GPU 1: 99 93 87 81 75 69 63 57 51 45 39 33 27 21 15 9 3
    GPU 2: 98 92 86 80 74 68 62 56 50 44 38 32 26 20 14 8 2
    GPU 3: 97 91 85 79 73 67 61 55 49 43 37 31 25 19 13 7 1
    GPU 4: 96 90 84 78 72 66 60 54 48 42 36 30 24 18 12 6 0
    GPU 5: 95 89 83 77 71 65 59 53 47 41 35 29 23 17 11 5 0

    Below, we collate the results into a single array containing "100 99 98 [...]".
  */
  {
    unsigned int total_results = args[0].num_results * num_devices;
    if (total_results >= args[0].chain_len - 1)
      total_results = args[0].chain_len - 1;

    output_index = total_results;

    unsigned int ri = total_results - 1;
    for (i = 0; i < args[0].num_results; i++) {
      for (j = 0; j < num_devices; j++) {
        if (ri < total_results)
          output[ri] = args[j].results[i];
        if (ri == 0)
          goto collation_done;
        ri--;
      }
    }
    collation_done: ;
  }

  /* Now that pulled all the GPU results into one array, free them. */
  for (i = 0; i < num_devices; i++) {
    FREE(args[i].results);
    args[i].num_results = 0;
  }

  /* Ensure we didn't get all zeros. */
  for (k = 0; k < output_index; k++)
    if (output[k] != 0)
      break;

  if (k == output_index) {
    fprintf(stderr, "Error: all zeros in precomputation!\n");
    exit(-1);
  }

  total_precomputed_indices_loaded += output_index;

  /* Persist to the precompute disk cache so a subsequent run can skip the
   * GPU dispatch entirely.  Matches the blurbdust on-disk format (raw
   * gpu_ulong array, length output_index = chain_len-1). */
  if (index_data != NULL)
    save_precompute_cache(index_data, output, output_index);

  if (update_ppi != NULL) {
    /* Update an existing ppi node for a new table configuration. */
    FREE(update_ppi->precomputed_end_indices);
    update_ppi->num_precomputed_end_indices = output_index;
    update_ppi->precomputed_end_indices = calloc(output_index, sizeof(gpu_ulong));
    if (update_ppi->precomputed_end_indices == NULL) {
      fprintf(stderr, "Error allocating index buffer for precomputed indices.\n");
      exit(-1);
    }
    for (i = 0; i < output_index; i++)
      update_ppi->precomputed_end_indices[i] = output[i];
  } else {
    /* Original behaviour: append a new ppi node to ppi_head. */
    if (*ppi_head == NULL) {
      *ppi_head = calloc(1, sizeof(precomputed_and_potential_indices));
      if (*ppi_head == NULL) {
        fprintf(stderr, "Error allocating buffer for precomputed indices.\n");
        exit(-1);
      }
      ppi = *ppi_head;
    } else {
      ppi = *ppi_head;
      while (ppi->next != NULL)
        ppi = ppi->next;
      ppi->next = calloc(1, sizeof(precomputed_and_potential_indices));
      if (ppi->next == NULL) {
        fprintf(stderr, "Error allocating buffer for precomputed indices.\n");
        exit(-1);
      }
      ppi = ppi->next;
    }

    ppi->username = args->username;
    ppi->hash = args->hash;
    ppi->num_precomputed_end_indices = output_index;

    ppi->precomputed_end_indices = calloc(ppi->num_precomputed_end_indices, sizeof(gpu_ulong));
    if (ppi->precomputed_end_indices == NULL) {
      fprintf(stderr, "Error allocating index buffer for precomputed indices.\n");
      exit(-1);
    }

    for (i = 0; i < ppi->num_precomputed_end_indices; i++)
      ppi->precomputed_end_indices[i] = output[i];
  }

  FREE(output);
}


static char **collect_table_paths(char *rt_dir, const rt_parameters *filter,
                                  unsigned int *out_count) {
  DIR *dir = NULL;
  struct dirent *de = NULL;
  struct stat st;
  char filepath[512] = {0};
  unsigned int count = 0, capacity = 256;
  char **paths = calloc(capacity, sizeof(char *));

  dir = opendir(rt_dir);
  if (dir == NULL) { *out_count = 0; return paths; }

  while ((de = readdir(dir)) != NULL) {
    filepath_join(filepath, sizeof(filepath), rt_dir, de->d_name);

    if ((strcmp(de->d_name, ".") != 0) && (strcmp(de->d_name, "..") != 0) &&
        (stat(filepath, &st) == 0) && S_ISDIR(st.st_mode)) {
      unsigned int sub_count = 0;
      char **sub_paths = collect_table_paths(filepath, filter, &sub_count);
      for (unsigned int s = 0; s < sub_count; s++) {
        if (count >= capacity) { capacity *= 2; paths = realloc(paths, capacity * sizeof(char *)); }
        paths[count++] = sub_paths[s];
      }
      free(sub_paths);
      continue;
    }

    if (!str_ends_with(de->d_name, ".rt") && !str_ends_with(de->d_name, ".rtc") &&
        !str_ends_with(de->d_name, ".rti2"))
      continue;

    if (filter != NULL) {
      rt_parameters pt_params = {0};
      parse_rt_params(&pt_params, filepath);
      if (!pt_params.parsed || !configs_match(&pt_params, filter))
        continue;
    }

    if (count >= capacity) { capacity *= 2; paths = realloc(paths, capacity * sizeof(char *)); }
    paths[count++] = strdup(filepath);
  }
  closedir(dir);
  *out_count = count;
  return paths;
}


static int load_single_table(const char *filepath, preloaded_table *pt) {
  gpu_ulong *rainbow_table = NULL;
  uint64_t num_chains = 0;

  if (str_ends_with(filepath, ".rtc")) {
    if (rtc_decompress((char *)filepath, &rainbow_table, &num_chains) != 0)
      return -1;
  } else if (str_ends_with(filepath, ".rti2")) {
    if (rti2_decompress((char *)filepath, &rainbow_table, &num_chains) != 0)
      return -1;
  } else {
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) return -1;
    int64_t file_size = get_file_size(f);
    if ((file_size % (sizeof(gpu_ulong) * 2) != 0) || file_size <= 0) { fclose(f); return -1; }
    unsigned int num_longs = file_size / sizeof(gpu_ulong);
    rainbow_table = calloc(num_longs, sizeof(gpu_ulong));
    if (rainbow_table == NULL) { fclose(f); return -1; }
    if (fread(rainbow_table, sizeof(gpu_ulong), num_longs, f) != num_longs) {
      free(rainbow_table); fclose(f); return -1;
    }
    fclose(f);
    num_chains = num_longs / 2;

    if (!verify_rainbowtable(rainbow_table, num_chains, VERIFY_TABLE_TYPE_LOOKUP, 0, 0, NULL)) {
      free(rainbow_table);
      return -1;
    }
  }

  if (rainbow_table == NULL || num_chains == 0) return -1;

  pt->filepath = strdup(filepath);
  pt->rainbow_table = rainbow_table;
  pt->num_chains = num_chains;
  if (g_build_bloom) {
    pt->bf = bloom_create(num_chains, bloom_target_fpr);
    if (pt->bf != NULL) {
      for (uint64_t c = 0; c < num_chains; c++)
        bloom_insert(pt->bf, rainbow_table[(c * 2) + 1]);
    }
  } else {
    pt->bf = NULL;   /* auto-skip: search treats NULL bf as "no pre-filter" */
  }
  pt->next = NULL;
  return 0;
}


preloaded_table *get_preloaded_table() {
  preloaded_table *ret = NULL;

  pthread_mutex_lock(&preloaded_tables_lock);

  /* If no tables have been preloaded yet, wait until at least one becomes available. */
  while ((num_preloaded_tables_available == 0) && (table_loading_complete == 0))
    pthread_cond_wait(&condition_wait_for_tables, &preloaded_tables_lock);

  /* Return the head of the list. */
  ret = preloaded_table_list;

  /* If the head of the list isn't NULL, advance it by one. */
  if (preloaded_table_list != NULL) {
    preloaded_table_list = preloaded_table_list->next;

    if (num_preloaded_tables_available > 0)
      num_preloaded_tables_available--;

    /* Wake up the preloading thread if its waiting because it loaded the max.  Now that we're
     * consuming one table, it can load the next concurrently. */
    pthread_cond_signal(&condition_continue_loading_tables);
  }

  pthread_mutex_unlock(&preloaded_tables_lock);
  return ret;
}


/* Arguments handed to streaming_preloading_thread.  rt_dir and paths are owned
 * by the thread for its lifetime and freed before exit. */
typedef struct {
  char **paths;
  unsigned int num_paths;
} streaming_preload_args;

/* Shared state for the worker pool spawned by streaming_preloading_thread.
 * pool_lock guards next_path_idx only; the preloaded_table_list append and
 * throttle still go through preloaded_tables_lock as before.  The abort flag
 * is set by search_tables under preloaded_tables_lock when the consumer gives
 * up early (all hashes cracked) so workers can drop their in-flight pt
 * instead of publishing it into a drained, soon-to-be-NULLed list. */
typedef struct {
  char **paths;
  unsigned int num_paths;
  unsigned int next_path_idx;
  pthread_mutex_t pool_lock;
  volatile int abort;
} table_load_pool;

/* Module-level pointer to the currently active loader pool, published by
 * streaming_preloading_thread before spawning workers and cleared before it
 * returns.  Matches the existing one-active-config-at-a-time invariant of
 * preloaded_table_list / num_preloaded_tables_available — there is never more
 * than one streaming_lookup() in flight.  Read by search_tables under
 * preloaded_tables_lock to flip the abort flag during early-exit drain. */
static volatile table_load_pool *active_load_pool = NULL;

/* Return the number of worker threads to use for parallel table loading.
 * Defaults to min(8, online CPUs); $RCRT_LOAD_THREADS overrides in [1, 64]. */
static unsigned int compute_load_thread_count(void) {
  unsigned int n = 0;
  const char *env = getenv("RCRT_LOAD_THREADS");
  if (env != NULL && *env != '\0') {
    long v = strtol(env, NULL, 10);
    if (v >= 1 && v <= 64)
      return (unsigned int)v;
  }
  long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
  if (ncpu < 1) ncpu = 1;
  n = (unsigned int)ncpu;
  if (n > 8) n = 8;
  return n;
}

/* Initialize max_preload_num from $RCRT_MAX_PRELOAD if set (in [1, 256]),
 * else leave at MAX_PRELOAD_NUM_DEFAULT.  Each in-flight table holds ~1 GB
 * post-decompress on this workload, so the upper bound roughly caps RAM use. */
static void init_max_preload_num(void) {
  const char *env = getenv("RCRT_MAX_PRELOAD");
  if (env == NULL || *env == '\0') return;
  long v = strtol(env, NULL, 10);
  if (v >= 1 && v <= 256)
    max_preload_num = (unsigned int)v;
}

/* Worker for the parallel table loader.  Each iteration atomically claims the
 * next unclaimed path from the pool, loads it via load_single_table(), and
 * appends the resulting preloaded_table to the global list under
 * preloaded_tables_lock.  Workers share the same throttle as the original
 * single-threaded preloader: when num_preloaded_tables_available reaches
 * MAX_PRELOAD_NUM the worker waits on condition_continue_loading_tables until
 * the consumer drains one entry.  pthread_cond_signal from the consumer wakes
 * exactly one waiting worker; the others stay blocked, which is the desired
 * back-pressure behaviour. */
static void *table_load_worker(void *arg) {
  table_load_pool *pool = (table_load_pool *)arg;

  while (1) {
    unsigned int idx;
    pthread_mutex_lock(&pool->pool_lock);
    /* Bail before claiming another path if the consumer aborted; avoids one
     * wasted load_single_table() per worker after early exit. */
    if (pool->abort) {
      pthread_mutex_unlock(&pool->pool_lock);
      return NULL;
    }
    if (pool->next_path_idx >= pool->num_paths) {
      pthread_mutex_unlock(&pool->pool_lock);
      return NULL;
    }
    idx = pool->next_path_idx++;
    pthread_mutex_unlock(&pool->pool_lock);

    preloaded_table *pt = calloc(1, sizeof(preloaded_table));
    if (pt == NULL) {
      fprintf(stderr, "preloader: calloc failed\n");
      return NULL;
    }
    if (load_single_table(pool->paths[idx], pt) != 0) {
      fprintf(stderr, "Warning: skipping unloadable table: %s\n", pool->paths[idx]);
      free(pt);
      continue;
    }
    pt->next = NULL;

    pthread_mutex_lock(&preloaded_tables_lock);

    /* Throttle first: wait for room before appending so peak queue depth
     * stays at MAX_PRELOAD_NUM, not MAX_PRELOAD_NUM + (n_workers - 1). */
    while (num_preloaded_tables_available >= max_preload_num)
      pthread_cond_wait(&condition_continue_loading_tables, &preloaded_tables_lock);

    /* After waking from the throttle (or finding it open) the consumer may
     * have aborted: search_tables drains the list, zeroes the counter, sets
     * abort, then broadcasts.  Publishing pt now would leak it, since the
     * drained list is about to be NULLed without a follow-up free.  Drop it. */
    if (pool->abort) {
      pthread_mutex_unlock(&preloaded_tables_lock);
      bloom_free(pt->bf);
      pt->bf = NULL;
      FREE(pt->filepath);
      FREE(pt->rainbow_table);
      pt->num_chains = 0;
      FREE(pt);
      return NULL;
    }

    /* Append to tail. */
    if (preloaded_table_list == NULL) {
      preloaded_table_list = pt;
    } else {
      preloaded_table *cur = preloaded_table_list;
      while (cur->next != NULL) cur = cur->next;
      cur->next = pt;
    }
    num_preloaded_tables_available++;
    pthread_cond_signal(&condition_wait_for_tables);

    pthread_mutex_unlock(&preloaded_tables_lock);
  }

  return NULL;
}

/* Streaming preloader thread.  Dispatches a pool of worker threads which
 * concurrently load tables via load_single_table(), appending each one to the
 * global preloaded_table_list under preloaded_tables_lock.  Workers honour the
 * MAX_PRELOAD_NUM sliding-window throttle just like the original
 * single-threaded preloader.  Once all workers finish, this thread flips
 * table_loading_complete so the consumer (get_preloaded_table) drains and
 * returns NULL.
 *
 * Matches the producer-side contract of the original baseline preloader — the
 * existing search_tables() consumer path reads via get_preloaded_table() and
 * needs nothing else changed. */
static void *streaming_preloading_thread(void *ptr) {
  streaming_preload_args *args = (streaming_preload_args *)ptr;

  table_load_pool pool = {0};
  pool.paths = args->paths;
  pool.num_paths = args->num_paths;
  pool.next_path_idx = 0;
  pool.abort = 0;
  pthread_mutex_init(&pool.pool_lock, NULL);

  /* Publish the pool so search_tables() can flip the abort flag on early exit.
   * Safe because only one streaming_lookup() is ever in flight. */
  active_load_pool = &pool;

  unsigned int n_workers = compute_load_thread_count();
  if (n_workers > args->num_paths) n_workers = args->num_paths;

  pthread_t *worker_tids = NULL;
  if (n_workers > 0) {
    worker_tids = calloc(n_workers, sizeof(pthread_t));
    if (worker_tids == NULL) {
      fprintf(stderr, "Failed to allocate worker thread array\n");
      exit(-1);
    }
    for (unsigned int i = 0; i < n_workers; i++) {
      if (pthread_create(&worker_tids[i], NULL, table_load_worker, &pool) != 0) {
        fprintf(stderr, "Failed to spawn table loader worker %u\n", i);
        exit(-1);
      }
    }
    for (unsigned int i = 0; i < n_workers; i++)
      pthread_join(worker_tids[i], NULL);
    free(worker_tids);
  }

  /* Workers have all exited; safe to unpublish before we destroy the lock. */
  active_load_pool = NULL;

  pthread_mutex_destroy(&pool.pool_lock);

  /* Signal end-of-stream so get_preloaded_table() can return NULL. */
  pthread_mutex_lock(&preloaded_tables_lock);
  table_loading_complete = 1;
  pthread_cond_broadcast(&condition_wait_for_tables);
  pthread_mutex_unlock(&preloaded_tables_lock);

  for (unsigned int i = 0; i < args->num_paths; i++) free(args->paths[i]);
  free(args->paths);
  free(args);
  return NULL;
}

/* Streaming lookup: per-table sliding-window preload + batched GPU precompute
 * + simple search loop.  Carved-down replacement for pipelined_lookup that
 * trades the 27 GiB bulk-load RAM footprint for ~2 GiB streaming, with no
 * measurable wall-time loss — CPU rt_binary_search runs at ~0.1 s/table from
 * cold NVMe, so keeping all tables resident never pays off on workloads that
 * visit each table once.
 *
 * Reuses batch_precompute_all_hashes() (the chunk_size=8192 hot path) and
 * search_tables() (baseline-style streaming consumer) so almost no new code
 * is needed — this function is just glue. */
void streaming_lookup(char *rt_dir, const rt_parameters *filter,
                     unsigned int num_devices, thread_args *args,
                     char **hashes, char **usernames, unsigned int total_hashes,
                     precomputed_and_potential_indices **ppi_head) {

  struct timespec bench_presearch_start = {0};
  start_timer(&bench_presearch_start);

  /* Reset preloader globals for this config group. */
  pthread_mutex_lock(&preloaded_tables_lock);
  preloaded_table_list = NULL;
  num_preloaded_tables_available = 0;
  table_loading_complete = 0;
  pthread_mutex_unlock(&preloaded_tables_lock);

  /* Walk the directory once on the main thread; hand the path list to the
   * preloader.  Done here (not inside the thread) so we can print the total
   * for ETA and short-circuit if the directory is empty. */
  unsigned int total_paths = 0;
  char **all_paths = collect_table_paths(rt_dir, filter, &total_paths);
  if (total_paths == 0) {
    printf("No tables found for this config group.\n");
    free(all_paths);
    return;
  }
  printf("Found %u tables for this config group.\n", total_paths);
  fflush(stdout);

  /* Hand path ownership to the preloader thread. */
  streaming_preload_args *pargs = calloc(1, sizeof(streaming_preload_args));
  if (pargs == NULL) { fprintf(stderr, "streaming_lookup: calloc failed\n"); return; }
  pargs->paths = all_paths;
  pargs->num_paths = total_paths;

  /* Decide once whether per-table blooms are worth building for this config
   * group.  Few queries (few hashes) against huge tables => the per-table bloom
   * build costs more than it saves, so skip it (the search handles bf==NULL).
   * Estimate queries as total_hashes * chain_len (known now; the actual
   * precompute runs after the preloader starts). */
  if (bloom_fpr_forced) {
    g_build_bloom = (bloom_target_fpr > 0.0);
  } else {
    uint64_t est_queries = (uint64_t)total_hashes * (uint64_t)filter->chain_len;
    g_build_bloom = (bloom_target_fpr > 0.0) &&
                    bloom_is_worthwhile(est_queries, filter->num_chains, bloom_target_fpr);
  }
  printf("Bloom filter: %s (fpr=%g, ~%u hashes vs %"PRIu64" chains/table).\n",
         g_build_bloom ? "enabled" :
           (bloom_target_fpr <= 0.0 ? "disabled (--bloom-fpr 0)" : "auto-skipped (too few hashes to amortize build)"),
         bloom_target_fpr, total_hashes, filter->num_chains);
  fflush(stdout);

  pthread_t preloader_tid;
  if (pthread_create(&preloader_tid, NULL, streaming_preloading_thread, pargs) != 0) {
    fprintf(stderr, "Failed to spawn preloader thread\n");
    for (unsigned int i = 0; i < total_paths; i++) free(all_paths[i]);
    free(all_paths);
    free(pargs);
    return;
  }

  /* Collect uncracked hashes. */
  unsigned int num_uncracked = 0;
  char **uncracked_hashes = calloc(total_hashes, sizeof(char *));
  char **uncracked_usernames = calloc(total_hashes, sizeof(char *));
  for (unsigned int i = 0; i < total_hashes; i++) {
    precomputed_and_potential_indices *existing = ppi_find(*ppi_head, hashes[i]);
    if (existing != NULL && existing->plaintext != NULL) continue;
    uncracked_hashes[num_uncracked] = hashes[i];
    uncracked_usernames[num_uncracked] = usernames[i];
    num_uncracked++;
  }

  if (num_uncracked == 0) {
    printf("All hashes cracked.\n");
    free(uncracked_hashes); free(uncracked_usernames);
    pthread_join(preloader_tid, NULL);
    return;
  }

  /* Batched GPU precompute. */
  struct timespec precomp_start = {0};
  start_timer(&precomp_start);
  printf("\n  Precomputing %u hashes...\n", num_uncracked);
  fflush(stdout);

  ppi_reset_endpoints(*ppi_head);

  /* Build the cache key for each uncracked hash and try the disk cache
   * first.  On hit, materialise a ppi node directly from the cached
   * unfiltered array (applying the same non-zero filter batch_precompute
   * applies) and skip the GPU dispatch for that hash.  Cache misses fall
   * through to the batch (or per-hash) precompute path. */
  char **index_data_array = calloc(num_uncracked, sizeof(char *));
  char **cache_miss_hashes = calloc(num_uncracked, sizeof(char *));
  char **cache_miss_usernames = calloc(num_uncracked, sizeof(char *));
  char **cache_miss_index_data = calloc(num_uncracked, sizeof(char *));
  if (index_data_array == NULL || cache_miss_hashes == NULL ||
      cache_miss_usernames == NULL || cache_miss_index_data == NULL) {
    fprintf(stderr, "streaming_lookup: calloc failed for cache arrays.\n");
    exit(-1);
  }

  unsigned int num_cache_hits = 0, num_cache_misses = 0;
  for (unsigned int i = 0; i < num_uncracked; i++) {
    char index_data[256] = {0};
    char cache_fn[128] = {0};
    unsigned int num_cached = 0;
    build_precompute_index_data(index_data, sizeof(index_data),
                                &args[0], uncracked_hashes[i]);
    index_data_array[i] = strdup(index_data);

    gpu_ulong *cached = search_precompute_cache(index_data, &num_cached,
                                                cache_fn, sizeof(cache_fn));
    if (cached == NULL) {
      /* Cache miss: queue this hash up for the batch (or per-hash) path. */
      cache_miss_hashes[num_cache_misses]     = uncracked_hashes[i];
      cache_miss_usernames[num_cache_misses]  = uncracked_usernames[i];
      cache_miss_index_data[num_cache_misses] = index_data_array[i];
      num_cache_misses++;
      continue;
    }

    /* Cache hit: build a ppi node from the cached array, applying the
     * same non-zero filter the batch path uses. */
    printf("  Using cached pre-computed indices for hash %s.\n", uncracked_hashes[i]);
    fflush(stdout);

    unsigned int count = 0;
    for (unsigned int p = 0; p < num_cached; p++)
      if (cached[p] != 0) count++;

    precomputed_and_potential_indices *ppi =
        calloc(1, sizeof(precomputed_and_potential_indices));
    if (ppi == NULL) { fprintf(stderr, "Error allocating ppi.\n"); exit(-1); }
    ppi->username = uncracked_usernames[i];
    ppi->hash = uncracked_hashes[i];
    ppi->num_precomputed_end_indices = count;
    ppi->precomputed_end_indices = calloc(count, sizeof(gpu_ulong));
    if (ppi->precomputed_end_indices == NULL) {
      fprintf(stderr, "Error allocating ppi indices.\n"); exit(-1);
    }
    unsigned int idx = 0;
    for (unsigned int p = 0; p < num_cached; p++)
      if (cached[p] != 0)
        ppi->precomputed_end_indices[idx++] = cached[p];

    /* Append to ppi_head (matches batch_precompute_all_hashes' behaviour). */
    if (*ppi_head == NULL) {
      *ppi_head = ppi;
    } else {
      precomputed_and_potential_indices *tail = *ppi_head;
      while (tail->next != NULL) tail = tail->next;
      tail->next = ppi;
    }

    free(cached);
    total_precomputed_indices_loaded += num_cached;
    num_hashes_precomputed++;
    num_cache_hits++;
  }

  if (num_cache_hits > 0)
    printf("  Cache: %u hit / %u miss\n", num_cache_hits, num_cache_misses);

  int used_batch = 0;
  if (num_cache_misses >= 2)
    used_batch = batch_precompute_all_hashes(num_devices, args,
        cache_miss_hashes, cache_miss_usernames, num_cache_misses,
        cache_miss_index_data, ppi_head);

  if (!used_batch && num_cache_misses > 0) {
    /* Per-hash fallback for hash types/charsets without a batch kernel,
     * or for the num_cache_misses == 1 case. */
    for (unsigned int i = 0; i < num_cache_misses; i++) {
      for (unsigned int j = 0; j < num_devices; j++) {
        args[j].username = cache_miss_usernames[i];
        args[j].hash = cache_miss_hashes[i];
      }
      precomputed_and_potential_indices *existing = ppi_find(*ppi_head, cache_miss_hashes[i]);
      precompute_hash(num_devices, args, ppi_head, existing, cache_miss_index_data[i]);
    }
  }

  /* release_precompute_gpu is a no-op when no kernel was dispatched (the
   * precompute_gpu_ready flag stays clear), so it's safe to call even on
   * an all-cache-hit run. */
  release_precompute_gpu(num_devices, args);

  for (unsigned int i = 0; i < num_uncracked; i++)
    free(index_data_array[i]);
  free(index_data_array);
  free(cache_miss_hashes);
  free(cache_miss_usernames);
  free(cache_miss_index_data);

  char precomp_time_str[128] = {0};
  seconds_to_human_time(precomp_time_str, sizeof(precomp_time_str),
                        get_elapsed(&precomp_start));
  printf("  Precompute finished in %s.\n\n", precomp_time_str);
  fflush(stdout);

  printf("[bench] streaming_lookup pre-search: %.1f ms\n",
         get_elapsed(&bench_presearch_start) * 1000.0);
  fflush(stdout);

  /* Hand off to the streaming search loop.  It consumes preloaded_table_list
   * via get_preloaded_table() and runs CPU rt_binary_search + GPU false alarm
   * check per table. */
  search_tables(total_paths, *ppi_head, args);

  release_false_alarm_gpu(num_devices, args);

  free(uncracked_hashes);
  free(uncracked_usernames);
  pthread_join(preloader_tid, NULL);
}


/* Given the number of hashes processed out of the total, prints the estimated time left to
 * completion. */
void print_eta_precompute() {
  char eta_str[64] = {0};

  strncpy(eta_str, "Unknown", sizeof(eta_str) - 1);
  if ((num_hashes_precomputed > 0) && (num_hashes_precomputed_total >= num_hashes_precomputed)) {
    double seconds_per_hash = (double)(get_elapsed(&precompute_start_time) / (double)num_hashes_precomputed);
    unsigned int num_hashes_left = num_hashes_precomputed_total - num_hashes_precomputed;
    unsigned int num_seconds_left = num_hashes_left * seconds_per_hash;

    seconds_to_human_time(eta_str, sizeof(eta_str), num_seconds_left);
  }
  printf("  Estimated time to complete pre-computation (at most): %s\n\n", eta_str); fflush(stdout);
}


/* Given the number of tables processed out of the total, prints the estimated time left to
 * completion. */
void print_eta_search(unsigned int num_tables_processed, unsigned int num_tables_total) {
  char eta_str[64] = {0};

  /* Defensive: if the timer was never initialized, get_elapsed would
   * return CLOCK_MONOTONIC's absolute value (uptime on Linux), which
   * produces nonsense ETAs.  Bail out early in that case. */
  if (search_start_time.tv_sec == 0 && search_start_time.tv_nsec == 0) {
    printf("  Estimated time remaining (at most): Unknown\n");
    fflush(stdout);
    return;
  }

  strncpy(eta_str, "Unknown", sizeof(eta_str) - 1);
  if ((num_tables_processed > 0) && (num_tables_total >= num_tables_processed)) {
    double seconds_per_table = (double)(get_elapsed(&search_start_time) / (double)num_tables_processed);
    unsigned int num_tables_left = num_tables_total - num_tables_processed;
    unsigned int num_seconds_left = num_tables_left * seconds_per_table;

    seconds_to_human_time(eta_str, sizeof(eta_str), num_seconds_left);
  }
  printf("  Estimated time remaining (at most): %s\n", eta_str); fflush(stdout);
}


void print_usage_and_exit(char *prog_name, int exit_code) {
#ifdef _WIN32
  char *dir1 = "D:\\rt_ntlm\\";
  char *dir2 = "C:\\Users\\jsmith\\Desktop\\";
#else
  char *dir1 = "/export/rt_ntlm/";
  char *dir2 = "/home/user/";
#endif

  fprintf(stderr, "%sUsage:%s %s rainbow_table_directory (single_hash | filename_with_many_hashes.txt) [-gws GWS] [-disable-platform N]\n\n", WHITEB, CLR, prog_name);
  fprintf(stderr, "    %s-gws GWS%s    (Optional) Sets the global work size for each GPU.  This can significantly affect the speed.  To tune this setting, start with multiplying the max compute units by the max work group size (both are reported on program start-up).  Then increase/decrease the value and time the results.  For example, if the max compute units is 20, and the max work group size is 1024, try using 20 x 1024 = 20480, then 20480 - 1024 = 19456, 20480 - 2048 = 18432, 2048 + 1024 = 21504, etc.  If you find a value that works better than the automatic setting, please report your findings at: https://github.com/jtesta/rainbowcrackalack/issues\n\n", WHITEB, CLR);
  fprintf(stderr, "    %s-disable-platform N%s    (Optional) Disables a platform from being used (platform numbers are reported on program start-up).  Useful when experiencing strange problems on mixed-GPU systems.  Try disabling each platform one at a time and see if the program behaves normally.\n\n", WHITEB, CLR);
  fprintf(stderr, "    %s--fa-batch N%s    (Optional) False-alarm batch flush threshold (default 16384; 1 disables batching).\n\n", WHITEB, CLR);
  fprintf(stderr, "    %s--bloom-fpr X%s    (Optional) Bloom filter target false-positive rate (default 0.01; 0 disables).\n\n", WHITEB, CLR);
  fprintf(stderr, "    %s--gpu-search%s    (Optional) Offload per-table endpoint binary search to the GPU.  Off by default while still under validation.\n\n", WHITEB, CLR);
  fprintf(stderr, "    %s--challenge HEX%s    (Optional) NetNTLMv1 server challenge as 16 hex digits (default 1122334455667788).  Normally adopted automatically from the loaded tables.\n\n", WHITEB, CLR);
  fprintf(stderr, "%sExamples:%s\n    %s %s 64f12cddaa88057e06a81b54e73b949b\n    %s %s %shashes_one_per_line.txt\n    %s %s %spwdump.txt\n\n", WHITEB, CLR, prog_name, dir1, prog_name, dir1, dir2, prog_name, dir1, dir2);
  exit(exit_code);
}


unsigned int _rt_binary_search(gpu_ulong *rainbow_table, uint64_t low, uint64_t high, gpu_ulong search_index, gpu_ulong *start) {
  uint64_t chain = 0;

  while (high - low > 16) {
    uint64_t mid = ((high - low) / 2) + low;
    if (search_index >= rainbow_table[(mid * 2) + 1])
      low = mid;
    else
      high = mid;
  }

  uint64_t remaining = high - low;
  chain = low;

  while (remaining >= 4) {
    gpu_ulong e0 = rainbow_table[(chain * 2) + 1];
    gpu_ulong e1 = rainbow_table[((chain + 1) * 2) + 1];
    gpu_ulong e2 = rainbow_table[((chain + 2) * 2) + 1];
    gpu_ulong e3 = rainbow_table[((chain + 3) * 2) + 1];

    if (e0 == search_index) { *start = rainbow_table[chain * 2]; return 1; }
    if (e1 == search_index) { *start = rainbow_table[(chain + 1) * 2]; return 1; }
    if (e2 == search_index) { *start = rainbow_table[(chain + 2) * 2]; return 1; }
    if (e3 == search_index) { *start = rainbow_table[(chain + 3) * 2]; return 1; }

    chain += 4;
    remaining -= 4;
  }

  for (; chain < high; chain++) {
    if (search_index == rainbow_table[(chain * 2) + 1]) {
      *start = rainbow_table[chain * 2];
      return 1;
    }
  }

  return 0;
}


void *rt_binary_search_thread(void *ptr) {
  search_thread_args *args = (search_thread_args *)ptr;
  precomputed_and_potential_indices *ppi_cur = args->ppi_head;
  unsigned int i = 0;
  gpu_ulong start = 0;

  args->num_local_results = 0;

  unsigned int estimated_matches = 256;
  {
    precomputed_and_potential_indices *ppi_temp = args->ppi_head;
    unsigned int total_queries = 0;
    while (ppi_temp != NULL) {
      if (ppi_temp->plaintext == NULL)
        total_queries += ppi_temp->num_precomputed_end_indices;
      ppi_temp = ppi_temp->next;
    }
    estimated_matches = (total_queries / 100) + 1;
    if (estimated_matches < 256) estimated_matches = 256;
    if (estimated_matches > 65536) estimated_matches = 65536;
  }
  args->local_results_capacity = estimated_matches;

  args->local_results = calloc(args->local_results_capacity, sizeof(search_result_entry));
  if (args->local_results == NULL) {
    fprintf(stderr, "Failed to allocate thread-local search results buffer.\n");
    exit(-1);
  }

  while (ppi_cur != NULL) {
    if (ppi_cur->plaintext == NULL) {
      for (i = 0 + args->thread_number; i < ppi_cur->num_precomputed_end_indices; i += args->total_threads) {
	if (args->bf != NULL && !bloom_query(args->bf, ppi_cur->precomputed_end_indices[i]))
	  continue;
	if (_rt_binary_search(args->rainbow_table, 0, args->num_chains, ppi_cur->precomputed_end_indices[i], &start)) {
	  if (args->bf != NULL) bloom_record_confirmed(args->bf);
	  if (args->num_local_results == args->local_results_capacity) {
	    args->local_results_capacity *= 2;
	    args->local_results = realloc(args->local_results, args->local_results_capacity * sizeof(search_result_entry));
	    if (args->local_results == NULL) {
	      fprintf(stderr, "Failed to grow thread-local search results buffer.\n");
	      exit(-1);
	    }
	  }
	  args->local_results[args->num_local_results].ppi = ppi_cur;
	  args->local_results[args->num_local_results].start = start;
	  args->local_results[args->num_local_results].position = i;
	  args->num_local_results++;
	}
      }
    }
    ppi_cur = ppi_cur->next;
  }

  pthread_exit(NULL);
  return NULL;
}


/* Print per-table bloom stats and update the global aggregate.  Safe
 * to call with bf == NULL (no-op). */
static void bloom_report_and_free(const char *filepath, bloom_filter *bf) {
  if (bf == NULL) return;
  uint64_t q = 0, p = 0, c = 0, nbits = 0;
  unsigned int nhash = 0;
  bloom_get_stats(bf, &q, &p, &c, &nbits, &nhash);

  double obs_fpr = 0.0;
  uint64_t denom = (q > c) ? (q - c) : 0;
  uint64_t fp    = (p > c) ? (p - c) : 0;
  if (denom > 0) obs_fpr = 100.0 * (double)fp / (double)denom;

  fprintf(stderr, "[bloom] %s: queries=%llu passes=%llu confirmed=%llu observed_fpr=%.4f%% bits=%llu hashes=%u\n",
          filepath ? filepath : "(unknown)",
          (unsigned long long)q, (unsigned long long)p, (unsigned long long)c,
          obs_fpr, (unsigned long long)nbits, nhash);

  g_bloom_agg.queries   += q;
  g_bloom_agg.passes    += p;
  g_bloom_agg.confirmed += c;
  g_bloom_agg.tables    += 1;

  bloom_free(bf);
}


/* Rainbow table binary search.  Searches a table's end indices for any matches with
 * precomputed end indices.  If/when matches are found, the corresponding start indices
 * are added to the precomputed_and_potential_indices's potential_start_indices
 * array. */
void rt_binary_search(gpu_ulong *rainbow_table, uint64_t num_chains, bloom_filter *bf, precomputed_and_potential_indices *ppi_head) {
  struct timespec start_time_searching = {0};
  char time_searching_str[64] = {0};
  unsigned int num_threads = get_num_cpu_cores();
  {
    const char *env = getenv("RCRT_RT_BIN_THREADS");
    if (env != NULL && *env != '\0') {
      long v = strtol(env, NULL, 10);
      if (v >= 1 && v <= 256) num_threads = (unsigned int)v;
    }
  }
  pthread_t *threads = NULL;
  search_thread_args *args = NULL;
  unsigned int i = 0;
  double s_time = 0;


  start_timer(&start_time_searching);
  args = calloc(num_threads, sizeof(search_thread_args));
  threads = calloc(num_threads, sizeof(pthread_t));
  if ((args == NULL) || (threads == NULL)) {
    fprintf(stderr, "Failed to create thread/args for searching.\n");
    exit(-1);
  }

  printf("  Searching table for matching endpoints...\n");  fflush(stdout);

  for (i = 0; i < num_threads; i++) {
    args[i].thread_number = i;
    args[i].total_threads = num_threads;
    args[i].rainbow_table = rainbow_table;
    args[i].num_chains = num_chains;
    args[i].bf = bf;
    args[i].ppi_head = ppi_head;

    if (pthread_create(&(threads[i]), NULL, &rt_binary_search_thread, &(args[i]))) {
      perror("Failed to create thread");
      exit(-1);
    }
  }

  /* Wait for all threads to finish. */
  for (i = 0; i < num_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      perror("Failed to join with thread");
      exit(-1);
    }
  }

  /* Merge per-thread results into the ppi structures. */
  for (i = 0; i < num_threads; i++) {
    unsigned int r;
    for (r = 0; r < args[i].num_local_results; r++) {
      search_result_entry *entry = &args[i].local_results[r];
      add_potential_start_index_and_position(entry->ppi, entry->start, entry->position);
    }
    FREE(args[i].local_results);
  }

  s_time = get_elapsed(&start_time_searching);
  if (s_time < 1.0)
    printf("  Table searched in %.1f ms.\n", s_time * 1000.0);
  else {
    seconds_to_human_time(time_searching_str, sizeof(time_searching_str), s_time);
    printf("  Table searched in %s.\n", time_searching_str);
  }
  fflush(stdout);

  time_searching += s_time;
  FREE(args);
  FREE(threads);
}


/* --- GPU-accelerated rainbow-table endpoint binary search --------------- */

/* Cached GPU state for gpu_binary_search_streaming.  Initialised lazily on
 * the first call; torn down by gpu_binary_search_release() at the end of
 * search_tables().  We deliberately use a private context/queue here rather
 * than reusing args[].gpu.context/queue: the FA-check worker thread owns
 * those (it created them under its own thread-attached CUDA context), and
 * sharing would tangle stream ordering with the in-flight FA-check kernels
 * from the previous batch. */
static struct {
  int           ready;
  gpu_device    device;
  gpu_context   context;
  gpu_queue     queue;
  gpu_program   program;
  gpu_kernel    kernel;
} g_gpu_bsearch = {0};

/* Capacity (in match pairs) of the per-table GPU output buffer.  Each pair
 * is 16 bytes, so 4M pairs = 64 MB.  Confirmed matches per table on the
 * NetNTLMv1-7 bench workload are O(100) — multiple orders of magnitude
 * below this cap.  On overflow we transparently fall back to
 * rt_binary_search for that table so correctness is preserved. */
#define GPU_BSEARCH_OUT_CAP_PAIRS (4u * 1024u * 1024u)


void gpu_binary_search_release(void) {
  if (!g_gpu_bsearch.ready) return;
  CLRELEASEKERNEL(g_gpu_bsearch.kernel);
  CLRELEASEPROGRAM(g_gpu_bsearch.program);
  CLRELEASEQUEUE(g_gpu_bsearch.queue);
  CLRELEASECONTEXT(g_gpu_bsearch.context);
  g_gpu_bsearch.ready = 0;
}


/* GPU-offloaded variant of rt_binary_search.  On any setup/allocation
 * failure (no devices, GPU OOM, kernel-launch error path, or output
 * overflow) the function transparently falls back to rt_binary_search
 * so the caller always sees identical results.  See the design notes
 * in CUDA/gpu_binary_search.cu for the kernel's arg layout and
 * shared-output-buffer scheme. */
void gpu_binary_search_streaming(preloaded_table *pt,
                                 precomputed_and_potential_indices *ppi_head,
                                 thread_args *args, unsigned int num_devices) {
  if (num_devices == 0 || pt == NULL || pt->num_chains == 0 ||
      pt->rainbow_table == NULL) {
    rt_binary_search(pt ? pt->rainbow_table : NULL,
                     pt ? pt->num_chains : 0,
                     pt ? pt->bf : NULL, ppi_head);
    return;
  }

  /* Count total uncracked end indices across all ppis.  Same data the
   * CPU rt_binary_search would walk. */
  unsigned int total_end_indices = 0;
  precomputed_and_potential_indices *ppi_cur = ppi_head;
  while (ppi_cur != NULL) {
    if (ppi_cur->plaintext == NULL)
      total_end_indices += ppi_cur->num_precomputed_end_indices;
    ppi_cur = ppi_cur->next;
  }

  /* Tiny / empty workload — just use the CPU path. */
  if (total_end_indices < 2) {
    rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
    return;
  }

  struct timespec t_total_start = {0};
  start_timer(&t_total_start);

  /* Lazily build the cached context/queue/kernel on first call.  CLCREATECONTEXT
   * and CLCREATEQUEUE both expand to expressions that touch a local `err` int,
   * so declare it up front to keep all backends happy. */
  if (!g_gpu_bsearch.ready) {
    int init_err = 0; (void)init_err;
    g_gpu_bsearch.device = args[0].gpu.device;
    {
      int err = 0; (void)err;
      g_gpu_bsearch.context = CLCREATECONTEXT(context_callback, &g_gpu_bsearch.device);
      if (g_gpu_bsearch.context == NULL) {
        fprintf(stderr, "  GPU search: context create failed; falling back to CPU.\n");
        rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
        return;
      }
      g_gpu_bsearch.queue = CLCREATEQUEUE(g_gpu_bsearch.context, g_gpu_bsearch.device);
      if (g_gpu_bsearch.queue == NULL) {
        fprintf(stderr, "  GPU search: queue create failed; falling back to CPU.\n");
        CLRELEASECONTEXT(g_gpu_bsearch.context);
        rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
        return;
      }
    }
    load_kernel(g_gpu_bsearch.context, 1, &g_gpu_bsearch.device,
                GPU_BINARY_SEARCH_KERNEL_PATH, "gpu_binary_search",
                &g_gpu_bsearch.program, &g_gpu_bsearch.kernel,
                args[0].hash_type);
    if (g_gpu_bsearch.program == NULL || g_gpu_bsearch.kernel == NULL) {
      fprintf(stderr, "  GPU search: kernel load failed; falling back to CPU.\n");
      CLRELEASEQUEUE(g_gpu_bsearch.queue);
      CLRELEASECONTEXT(g_gpu_bsearch.context);
      rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
      return;
    }
    g_gpu_bsearch.ready = 1;
    printf("  GPU search: initialised dedicated context for endpoint search.\n");
    fflush(stdout);
  }

  /* Pack end indices into a flat host buffer + remember each entry's
   * originating ppi and position within that ppi's precomputed_end_indices.
   * This mirrors the (ppi, position) pair that rt_binary_search records
   * for each match. */
  gpu_ulong *h_end_indices = malloc((size_t)total_end_indices * sizeof(gpu_ulong));
  precomputed_and_potential_indices **entry_ppi =
      malloc((size_t)total_end_indices * sizeof(precomputed_and_potential_indices *));
  unsigned int *entry_pos = malloc((size_t)total_end_indices * sizeof(unsigned int));
  if (h_end_indices == NULL || entry_ppi == NULL || entry_pos == NULL) {
    fprintf(stderr, "  GPU search: host buffer alloc failed; falling back to CPU.\n");
    free(h_end_indices); free(entry_ppi); free(entry_pos);
    rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
    return;
  }

  unsigned int packed = 0;
  ppi_cur = ppi_head;
  while (ppi_cur != NULL) {
    if (ppi_cur->plaintext == NULL) {
      for (unsigned int i = 0; i < ppi_cur->num_precomputed_end_indices; i++) {
        h_end_indices[packed] = ppi_cur->precomputed_end_indices[i];
        entry_ppi[packed] = ppi_cur;
        entry_pos[packed] = i;
        packed++;
      }
    }
    ppi_cur = ppi_cur->next;
  }
  /* packed should equal total_end_indices; harmless if it differs (we use packed). */

  /* Macro plumbing: CLCREATEARG_* expects local `context`, `queue`, `kernel`,
   * and `err` symbols.  Bind them to the cached objects. */
  gpu_context context = g_gpu_bsearch.context;
  gpu_queue queue = g_gpu_bsearch.queue;
  gpu_kernel kernel = g_gpu_bsearch.kernel;
  int err = 0;
  (void)err;  /* silence unused-var warnings on backends where macros skip it */

  /* --- Upload rainbow table (per-call, freed before return). --- */
  size_t table_bytes = (size_t)pt->num_chains * 2 * sizeof(gpu_ulong);
  gpu_buffer rt_buf = 0, num_chains_buf = 0, bf_bits_buf = 0, bf_mask_buf = 0;
  gpu_buffer bf_num_hashes_buf = 0, end_indices_buf = 0, total_buf = 0;
  gpu_buffer cap_buf = 0, head_buf = 0, results_buf = 0;

  CLCREATEARG_ARRAY(0, rt_buf, GPU_RO, pt->rainbow_table, table_bytes);

  gpu_ulong num_chains_val = pt->num_chains;
  CLCREATEARG(1, num_chains_buf, GPU_RO, num_chains_val, sizeof(gpu_ulong));

  /* --- Bloom upload (only if a bloom is attached). --- */
  gpu_uint bf_num_hashes_val = 0;
  gpu_ulong bf_mask_val = 0;
  if (pt->bf != NULL && pt->bf->num_hashes > 0 && pt->bf->num_bits > 0) {
    size_t bf_bytes = (size_t)(pt->bf->num_bits / 8);
    CLCREATEARG_ARRAY(2, bf_bits_buf, GPU_RO, pt->bf->bits, bf_bytes);
    bf_mask_val = pt->bf->mask;
    bf_num_hashes_val = pt->bf->num_hashes;
  } else {
    /* Bloom disabled: feed a 1-byte stub so the buffer arg is bound, and
     * set num_hashes=0 to make the kernel skip the prefilter. */
    unsigned char stub = 0;
    CLCREATEARG_ARRAY(2, bf_bits_buf, GPU_RO, &stub, 1);
  }
  CLCREATEARG(3, bf_mask_buf, GPU_RO, bf_mask_val, sizeof(gpu_ulong));
  CLCREATEARG(4, bf_num_hashes_buf, GPU_RO, bf_num_hashes_val, sizeof(gpu_uint));

  /* --- Inputs / outputs --- */
  CLCREATEARG_ARRAY(5, end_indices_buf, GPU_RO,
                    h_end_indices, (size_t)total_end_indices * sizeof(gpu_ulong));
  gpu_uint total_val = total_end_indices;
  CLCREATEARG(6, total_buf, GPU_RO, total_val, sizeof(gpu_uint));

  gpu_uint out_cap_val = GPU_BSEARCH_OUT_CAP_PAIRS;
  CLCREATEARG(7, cap_buf, GPU_RO, out_cap_val, sizeof(gpu_uint));

  /* out_head: single atomic counter, host-zeroed. */
  gpu_uint zero = 0;
  CLCREATEARG(8, head_buf, GPU_RW, zero, sizeof(gpu_uint));

  /* out_results: 2 * cap ulongs (chain_index, entry_index per match). */
  size_t results_bytes = (size_t)out_cap_val * 2 * sizeof(gpu_ulong);
  gpu_ulong *h_results = malloc(results_bytes);
  if (h_results == NULL) {
    fprintf(stderr, "  GPU search: results host alloc (%.0f MB) failed; falling back to CPU.\n",
            (double)results_bytes / 1048576.0);
    CLFREEBUFFER(rt_buf); CLFREEBUFFER(num_chains_buf);
    CLFREEBUFFER(bf_bits_buf); CLFREEBUFFER(bf_mask_buf); CLFREEBUFFER(bf_num_hashes_buf);
    CLFREEBUFFER(end_indices_buf); CLFREEBUFFER(total_buf);
    CLFREEBUFFER(cap_buf); CLFREEBUFFER(head_buf);
    free(h_end_indices); free(entry_ppi); free(entry_pos);
    rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
    return;
  }
  CLCREATEARG_ARRAY(9, results_buf, GPU_RW, h_results, results_bytes);

  size_t gws = ((total_end_indices + 255) / 256) * 256;
  CLRUNKERNEL(queue, kernel, &gws);
  CLWAIT(queue);

  /* Read back the head + (capped) results. */
  gpu_uint head_val = 0;
  CLREADBUFFER(head_buf, sizeof(gpu_uint), &head_val);

  unsigned int n_matches = (head_val > out_cap_val) ? out_cap_val : (unsigned int)head_val;
  if (n_matches > 0) {
    size_t read_bytes = (size_t)n_matches * 2 * sizeof(gpu_ulong);
    CLREADBUFFER(results_buf, read_bytes, h_results);
  }

  /* Free GPU buffers now -- we're done with the device for this table. */
  CLFREEBUFFER(rt_buf);
  CLFREEBUFFER(num_chains_buf);
  CLFREEBUFFER(bf_bits_buf);
  CLFREEBUFFER(bf_mask_buf);
  CLFREEBUFFER(bf_num_hashes_buf);
  CLFREEBUFFER(end_indices_buf);
  CLFREEBUFFER(total_buf);
  CLFREEBUFFER(cap_buf);
  CLFREEBUFFER(head_buf);
  CLFREEBUFFER(results_buf);

  if ((unsigned int)head_val > out_cap_val) {
    /* Should be extremely rare on real workloads.  Be loud and re-run on CPU
     * so the result set stays correct. */
    fprintf(stderr,
            "  GPU search: output buffer overflowed (%u > cap %u); re-running on CPU.\n",
            (unsigned int)head_val, out_cap_val);
    fflush(stderr);
    free(h_results);
    free(h_end_indices); free(entry_ppi); free(entry_pos);
    rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi_head);
    return;
  }

  /* Translate each (chain_index, entry_index) into the same (start, position)
   * pair rt_binary_search would insert via add_potential_start_index_and_position. */
  for (unsigned int m = 0; m < n_matches; m++) {
    gpu_ulong chain_idx = h_results[(size_t)m * 2 + 0];
    unsigned int entry_idx = (unsigned int)h_results[(size_t)m * 2 + 1];
    if (entry_idx >= total_end_indices || chain_idx >= pt->num_chains) {
      /* Defensive: should never happen unless the kernel mis-wrote. */
      fprintf(stderr,
              "  GPU search: bogus result (entry_idx=%u, chain_idx=%llu); skipping.\n",
              entry_idx, (unsigned long long)chain_idx);
      continue;
    }
    gpu_ulong start = pt->rainbow_table[chain_idx * 2];
    add_potential_start_index_and_position(entry_ppi[entry_idx], start,
                                           entry_pos[entry_idx]);
    if (pt->bf != NULL) bloom_record_confirmed(pt->bf);
  }

  free(h_results);
  free(h_end_indices); free(entry_ppi); free(entry_pos);

  double s_time = get_elapsed(&t_total_start);
  if (s_time < 1.0)
    printf("  Table GPU-searched in %.1f ms (%u match%s).\n",
           s_time * 1000.0, n_matches, n_matches == 1 ? "" : "es");
  else
    printf("  Table GPU-searched in %.2f s (%u match%s).\n",
           s_time, n_matches, n_matches == 1 ? "" : "es");
  fflush(stdout);
  time_searching += s_time;
}


void save_cracked_hash(precomputed_and_potential_indices *ppi, unsigned int hash_type) {
  FILE *jtr_file = fopen(jtr_pot_filename, "ab"), *hashcat_file = fopen(hashcat_pot_filename, "ab");
  unsigned int hash_len = strlen(ppi->hash);
  unsigned int plaintext_len = strlen(ppi->plaintext);


  if (jtr_file == NULL) {
    fprintf(stderr, "Error: could not open pot file for writing: %s: %s\n", jtr_pot_filename, strerror(errno));
    exit(-1);
  } else if (hashcat_file == NULL) {
    fprintf(stderr, "Error: could not open pot file for writing: %s: %s\n", hashcat_pot_filename, strerror(errno));
    exit(-1);
  }

  /* The JTR pot file format requires NTLM hashes to be prepended with "$NT$". */
  if ((hash_type == HASH_NTLM) && (fwrite("$NT$", sizeof(char), 4, jtr_file) != 4)) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  }

  if (fwrite(ppi->hash, sizeof(char), hash_len, jtr_file) != hash_len) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  } else if (fwrite(ppi->hash, sizeof(char), hash_len, hashcat_file) != hash_len) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  }

  if (fwrite(":", sizeof(char), 1, jtr_file) != 1) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  } else if (fwrite(":", sizeof(char), 1, hashcat_file) != 1) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  }

  if (fwrite(ppi->plaintext, sizeof(char), plaintext_len, jtr_file) != plaintext_len) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  } else if (fwrite(ppi->plaintext, sizeof(char), plaintext_len, hashcat_file) != plaintext_len) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  }

  if (fwrite("\n", sizeof(char), 1, jtr_file) != 1) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  } else if (fwrite("\n", sizeof(char), 1, hashcat_file) != 1) {
    fprintf(stderr, "Error while writing to pot file: %s\n", strerror(errno));
    exit(-1);
  }

  FCLOSE(jtr_file);
  FCLOSE(hashcat_file);

  num_cracked++;
  num_falsealarms--;
}


void search_tables(unsigned int total_tables, precomputed_and_potential_indices *ppi, thread_args *args) {
  unsigned int num_uncracked = 0, current_table = 0;
  struct timespec start_time_table = {0};
  precomputed_and_potential_indices *ppi_cur = NULL;
  preloaded_table *pt = NULL;
  false_alarm_state fa_state = {0};

  /* num_cracked is only updated during FA-batch harvest, which happens at
   * flush boundaries (every ~16k candidates).  Track what we've printed so
   * we only emit the line when the count actually changes. */
  unsigned int num_cracked_last_printed = num_cracked;

  struct timespec bench_phase_start = {0};
  double t_wait_table_cum = 0.0;
  double t_search_cum = 0.0;
  double t_accumulate_cum = 0.0;
  double t_harvest_cum = 0.0;
  double t_launch_cum = 0.0;
  double t_reset_cum = 0.0;
  double t_cleanup = 0.0;
  double t_drain_launch = 0.0;
  double t_drain_harvest = 0.0;
  unsigned int n_flushes = 0;
  unsigned int n_tables_bench = 0;

  fa_batch_t fa_batch = {0};
  if (fa_batch_init(&fa_batch, fa_batch_threshold, 0) != 0) {
    fprintf(stderr, "fa_batch_init failed\n"); exit(-1);
  }

  /* Compute plaintext_space_total once per search_tables call (same logic
   * as the old per-launch computation, now hoisted out of the dispatch path). */
  /* plaintext_space_up_to_index is filled by fill_plaintext_space_* as a
   * required output buffer.  The filled per-length table is not consumed
   * here; only plaintext_space_total (the return value) is forwarded into
   * fa_batch_append.  Kept because the fill_* helpers require the array. */
  gpu_ulong plaintext_space_up_to_index[MAX_PLAINTEXT_LEN + 1] = {0};
  uint64_t plaintext_space_total = 0;
  int charset_len = 0;
  if (args[0].markov_keyspace > 0) {
    charset_len = strlen(args[0].charset);
    if (charset_len == 0) charset_len = 1;
    plaintext_space_total = fill_plaintext_space_markov_keyspace(
        args[0].markov_keyspace, args[0].plaintext_len_max, plaintext_space_up_to_index);
  } else {
    if (strcmp(args[0].charset_name, "byte") == 0)
      charset_len = 256;
    else
      charset_len = strlen(args[0].charset);
    plaintext_space_total = fill_plaintext_space_table(
        charset_len, args[0].plaintext_len_min, args[0].plaintext_len_max,
        plaintext_space_up_to_index);
  }
  (void)charset_len;  /* used only via fill_* above; suppress unused-var warning */

  start_timer(&search_start_time);

  while (1) {

    /* Count the number of uncracked hashes we have left.  Note: if a false
     * alarm check is still in-flight from the previous iteration, a newly
     * cracked hash may not be reflected here yet.  That is harmless -- we
     * simply do one extra table search for an already-cracked hash. */
    ppi_cur = ppi;
    num_uncracked = 0;
    while (ppi_cur != NULL) {
      if (ppi_cur->plaintext == NULL)
	num_uncracked++;

      ppi_cur = ppi_cur->next;
    }

    /* If all the hashes were cracked, there's no need to continue processing
     * tables. */
    if (num_uncracked == 0) {
      /* IMPORTANT: harvest must complete before fa_batch_reset.  GPU
       * threads borrow batch arrays for the duration of the in-flight
       * kernel; resetting (or freeing) before join would tear out memory
       * those threads are still reading. */
      harvest_false_alarm_results(&fa_state);
      printf("All hashes cracked.  Skipping rest of tables.\n");
      break;
    }

    /* Get the next preloaded table.  If NULL, we reached the end. */
    start_timer(&bench_phase_start);
    pt = get_preloaded_table();
    t_wait_table_cum += get_elapsed(&bench_phase_start);
    if (pt == NULL) {
      /* IMPORTANT: harvest must complete before fa_batch_reset.  GPU
       * threads borrow batch arrays for the duration of the in-flight
       * kernel; resetting (or freeing) before join would tear out memory
       * those threads are still reading. */
      harvest_false_alarm_results(&fa_state);
      break;
    }

    current_table++;
    printf("[%u of %u] Processing table: %s...\n", current_table, total_tables, pt->filepath);  fflush(stdout);

    start_timer(&start_time_table);

    /* CPU binary search for THIS table.  This runs concurrently with any
     * in-flight GPU false alarm kernel from the previous flush (if any).
     * Safe because:
     *   - binary search reads ppi->precomputed_end_indices (not touched by GPU)
     *   - binary search writes ppi->potential_start_indices.  fa_batch_append
     *     copies them into the batch's flat arrays in the same iteration;
     *     clear_potential_start_indices then frees the ppi-level originals.
     *     The batch arrays are what the GPU reads, so clearing the ppi
     *     originals is safe.
     *   - harvest (which modifies ppi->plaintext and frees precomputed_end_indices
     *     for cracked hashes) runs AFTER binary search completes, inside the
     *     flush block
     *
     * KNOWN TIMING-BOUNDED RACE:  the worker thread spawned by
     * launch_false_alarm_kernel performs its CLCREATEARG_ARRAY uploads
     * (start_indices, start_index_positions, hash_base_indices — kernel
     * arg indices 11/12/13) after pthread_create returns.  The next
     * iteration's fa_batch_append mutates those same arrays.  In
     * practice rt_binary_search is ~100ms while the upload is
     * sub-millisecond, so the worker always finishes its uploads first
     * — but there is no synchronization primitive enforcing it.  The
     * harvest-time CPU verification (ntlm_hash + strcmp on the
     * plaintext) catches any false positives the race could produce,
     * so worst case is a missed crack, not a wrong one.  TODO: move
     * those CLCREATEARG_ARRAY calls into launch_false_alarm_kernel on
     * the main thread so the upload is synchronous. */
    start_timer(&bench_phase_start);
    if (gpu_search_enabled) {
      /* GPU-offloaded endpoint binary search.  Falls back to
       * rt_binary_search() internally on any failure (no GPU/VRAM,
       * overflow, etc.) so the result set is always identical to the
       * CPU path. */
      gpu_binary_search_streaming(pt, ppi, args, args[0].total_devices);
    } else {
      rt_binary_search(pt->rainbow_table, pt->num_chains, pt->bf, ppi);
    }
    t_search_cum += get_elapsed(&bench_phase_start);
    n_tables_bench++;

    num_chains_processed += pt->num_chains;
    num_tables_processed++;

    /* Free the preloaded table -- binary search is done with it. */
    bloom_report_and_free(pt->filepath, pt->bf);
    pt->bf = NULL;
    FREE(pt->filepath);
    FREE(pt->rainbow_table);
    pt->num_chains = 0;
    FREE(pt);

    /* Append candidates from THIS table into the batch.  No GPU work yet. */
    start_timer(&bench_phase_start);
    if (fa_batch_append(&fa_batch, ppi, args[0].reduction_offset,
                        plaintext_space_total) != 0) {
      fprintf(stderr, "fa_batch_append failed\n"); exit(-1);
    }
    /* Safe to clear ppi now -- fa_batch_append has copied the indices. */
    clear_potential_start_indices(ppi);
    t_accumulate_cum += get_elapsed(&bench_phase_start);

    if (fa_batch_should_flush(&fa_batch, /*force=*/0)) {
      /* IMPORTANT: harvest must complete before fa_batch_reset.  GPU
       * threads borrow batch arrays for the duration of the in-flight
       * kernel; resetting (or freeing) before join would tear out memory
       * those threads are still reading. */
      start_timer(&bench_phase_start);
      harvest_false_alarm_results(&fa_state);
      t_harvest_cum += get_elapsed(&bench_phase_start);

      start_timer(&bench_phase_start);
      launch_false_alarm_kernel(&fa_batch, args, &fa_state);
      t_launch_cum += get_elapsed(&bench_phase_start);

      start_timer(&bench_phase_start);
      fa_batch_reset(&fa_batch);
      t_reset_cum += get_elapsed(&bench_phase_start);

      n_flushes++;
    }

    {
      double t = get_elapsed(&start_time_table);
      if (t < 1.0)
        printf("  Table processed in %.1f ms.\n", t * 1000.0);
      else
        printf("  Table processed in %.1f seconds.\n", t);
      fflush(stdout);
    }
    print_eta_search(num_tables_processed, total_tables);
    if (num_cracked != num_cracked_last_printed) {
      printf("  Cracked %u of %u hashes.\n\n", num_cracked, num_hashes);
      num_cracked_last_printed = num_cracked;
    } else {
      printf("\n");
    }

  }

  /* Drain any candidates below the flush threshold that were never launched.
   * With threshold-based flushing, up to (fa_batch_threshold - 1) candidates
   * can accumulate after the last flush.  Same harvest-before-reset rule applies. */
  if (fa_batch.num_candidates > 0) {
    start_timer(&bench_phase_start);
    launch_false_alarm_kernel(&fa_batch, args, &fa_state);
    t_drain_launch = get_elapsed(&bench_phase_start);

    start_timer(&bench_phase_start);
    harvest_false_alarm_results(&fa_state);
    t_drain_harvest = get_elapsed(&bench_phase_start);

    fa_batch_reset(&fa_batch);
  }
  if (num_cracked != num_cracked_last_printed) {
    printf("  Cracked %u of %u hashes.\n\n", num_cracked, num_hashes);
    num_cracked_last_printed = num_cracked;
  }
  printf("[bench] final drain: launch=%.1f ms harvest=%.1f ms\n",
         t_drain_launch * 1000.0, t_drain_harvest * 1000.0);
  fflush(stdout);

  struct timespec bench_cleanup_start = {0};
  start_timer(&bench_cleanup_start);

  fa_batch_free(&fa_batch);

  /* Tear down the GPU-binary-search context/queue/kernel cached by
   * gpu_binary_search_streaming, if any.  No-op when --gpu-search was off. */
  gpu_binary_search_release();

  /* Free any remaining preloaded tables (i.e.: if we cracked all the hashes and quit early).
   * In-flight workers are handled below via the pool's abort flag. */
  pthread_mutex_lock(&preloaded_tables_lock);
  while (preloaded_table_list != NULL) {
    preloaded_table *pt_next = preloaded_table_list->next;

    bloom_report_and_free(preloaded_table_list->filepath, preloaded_table_list->bf);
    preloaded_table_list->bf = NULL;
    FREE(preloaded_table_list->filepath);
    FREE(preloaded_table_list->rainbow_table);
    preloaded_table_list->num_chains = 0;
    FREE(preloaded_table_list);

    preloaded_table_list = pt_next;
  }
  /* With N parallel workers any number of them may be parked on the throttle;
   * reset the counter and broadcast so all of them wake and finish/exit.
   * Set the abort flag (under the same lock as the broadcast, ordered before
   * it) so workers that wake holding an in-flight pt drop it instead of
   * publishing into the now-drained list — otherwise streaming_lookup's
   * preloaded_table_list = NULL reset would leak those entries' rainbow
   * table and bloom allocations. */
  num_preloaded_tables_available = 0;
  if (active_load_pool != NULL)
    active_load_pool->abort = 1;
  pthread_cond_broadcast(&condition_continue_loading_tables);
  pthread_mutex_unlock(&preloaded_tables_lock);

  t_cleanup = get_elapsed(&bench_cleanup_start);
  printf("[bench] cleanup: %.1f ms\n", t_cleanup * 1000.0);
  fflush(stdout);

  printf("[bench] search_tables phases: wait=%.1fms search=%.1fms accumulate=%.1fms harvest=%.1fms launch=%.1fms reset=%.1fms cleanup=%.1fms n_tables=%u n_flushes=%u\n",
         t_wait_table_cum * 1000.0,
         t_search_cum * 1000.0,
         t_accumulate_cum * 1000.0,
         t_harvest_cum * 1000.0,
         t_launch_cum * 1000.0,
         t_reset_cum * 1000.0,
         t_cleanup * 1000.0,
         n_tables_bench,
         n_flushes);
  fflush(stdout);
}


int main(int ac, char **av) {
  char *rt_dir = NULL, *single_hash = NULL, *filename = NULL, *file_data = NULL, **usernames = NULL, **hashes = NULL, *line = NULL, *pot_file_data = NULL;
  unsigned int i = 0, j = 0, max_num_hashes = 0, num_colons = 0, file_format = 0, err = 0;
  FILE *f = NULL;
  struct stat st = {0};
  thread_args *args = NULL;
  char time_precomp_str[64] = {0}, time_io_str[64] = {0}, time_searching_str[64] = {0}, time_falsealarms_str[64] = {0}, time_total_str[64] = {0}, time_per_table_str[64] = {0};


  gpu_platform platforms[MAX_NUM_PLATFORMS] = {0};
  gpu_device devices[MAX_NUM_DEVICES] = {0};

  gpu_uint num_platforms = 0, num_devices = 0;

  precomputed_and_potential_indices *ppi_head = NULL, *ppi_cur = NULL;


  ENABLE_CONSOLE_COLOR();
  PRINT_PROJECT_HEADER();
  setlocale(LC_NUMERIC, "");
  init_max_preload_num();
  memcpy(g_challenge, NETNTLMV1_DEFAULT_CHALLENGE, 8);
  if (ac < 3)
    print_usage_and_exit(av[0], -1);

  /* Parse optional flags (everything after the first two positional args). */
  for (i = 3; i < (unsigned int)ac; i++) {
    if ((strcmp(av[i], "-gws") == 0) && (i + 1 < (unsigned int)ac)) {
      user_provided_gws = parse_uint_arg(av[++i], "-gws");
    } else if ((strcmp(av[i], "-disable-platform") == 0) && (i + 1 < (unsigned int)ac)) {
      disable_platform = parse_uint_arg(av[++i], "-disable-platform");
    } else if ((strcmp(av[i], "--markov") == 0) && (i + 1 < (unsigned int)ac)) {
      use_markov = 1;
      strncpy(markov_path, av[++i], sizeof(markov_path) - 1);
    } else if ((strcmp(av[i], "--fa-batch") == 0) && (i + 1 < (unsigned int)ac)) {
      unsigned int v = parse_uint_arg(av[++i], "--fa-batch");
      if (v == 0) v = 16384;       /* 0 means "use default" */
      fa_batch_threshold = v;
    } else if ((strcmp(av[i], "--bloom-fpr") == 0) && (i + 1 < (unsigned int)ac)) {
      char *end = NULL;
      errno = 0;
      double v = strtod(av[++i], &end);
      if (errno != 0 || end == av[i] || *end != '\0') {
        fprintf(stderr, "Error: --bloom-fpr must be a valid floating-point number, got '%s'.\n", av[i]);
        print_usage_and_exit(av[0], -1);
      }
      if (!(v >= 0.0 && v < 1.0)) {
        fprintf(stderr, "Error: --bloom-fpr must be in [0, 1), got %g.\n", v);
        print_usage_and_exit(av[0], -1);
      }
      bloom_target_fpr = v;
      bloom_fpr_forced = (v > 0.0);   /* explicit positive fpr forces the bloom on */
    } else if (strcmp(av[i], "--gpu-search") == 0) {
      gpu_search_enabled = 1;
    } else if (strcmp(av[i], "--challenge") == 0) {
      if (i + 1 >= (unsigned int)ac) {
        fprintf(stderr, "Error: --challenge requires a 16-hex value.\n");
        print_usage_and_exit(av[0], -1);
      }
      if (parse_challenge_str(av[++i], g_challenge) != 0) {
        fprintf(stderr, "Error: --challenge must be exactly 16 hex digits, got '%s'.\n", av[i]);
        print_usage_and_exit(av[0], -1);
      }
      g_challenge_set = 1;
    } else {
      /* Undocumented third arg: override pot filename (kept for backward compat). */
      if (i == 3 && av[i][0] != '-') {
        strncpy(jtr_pot_filename, av[i], sizeof(jtr_pot_filename) - 1);
        jtr_pot_filename[sizeof(jtr_pot_filename) - 1] = '\0';
        strncpy(hashcat_pot_filename, av[i], sizeof(hashcat_pot_filename) - 1);
        hashcat_pot_filename[sizeof(hashcat_pot_filename) - 1] = '\0';
        strncat(hashcat_pot_filename, ".hashcat", sizeof(hashcat_pot_filename) - strlen(hashcat_pot_filename) - 1);
      } else {
        fprintf(stderr, "Error: unrecognized argument: %s\n", av[i]);
        print_usage_and_exit(av[0], -1);
      }
    }
  }

  /* Initialize the devices. */
  get_platforms_and_devices(disable_platform, MAX_NUM_PLATFORMS, platforms, &num_platforms, MAX_NUM_DEVICES, devices, &num_devices, VERBOSE);

  /* Check the device type and set flags.*/
  if (num_devices > 0) {
    char device_vendor[128] = {0};

    get_device_str(devices[0], CL_DEVICE_VENDOR, device_vendor, sizeof(device_vendor) - 1);
    if (strstr(device_vendor, "Advanced Micro Devices") != NULL)
      is_amd_gpu = 1;
  }

  /* Print a warning on Windows 7 systems, as they are observed to be highly
   * unstable for performing lookups on. */
  PRINT_WIN7_LOOKUP_WARNING();

  /* Check that this system has sufficient RAM. */
  CHECK_MEMORY_SIZE();

  /* Initialize the barrier.  This is used in some cases to ensure kernels across
   * multiple devices run concurrently. */
  if (pthread_barrier_init(&barrier, NULL, num_devices) != 0) {
    fprintf(stderr, "pthread_barrier_init() failed.\n");
    exit(-1);
  }

  printf("Binary searching will be done with %u threads.\n", get_num_cpu_cores());

  /* First arg is the directory (and/or sub-directories) containing rainbow tables. */
  rt_dir = av[1];

  /* Open the JTR pot file for reading.  We will check the hash(es) to see if any are
   * already cracked. */
  f = fopen(jtr_pot_filename, "rb");
  if (f) {
    int64_t file_size = get_file_size(f);

    pot_file_data = calloc(file_size, sizeof(char));
    if (pot_file_data == NULL) {
      fprintf(stderr, "Failed to allocate buffer for pot file.\n");
      exit(-1);
    }

    if (fread(pot_file_data, sizeof(char), file_size, f) != file_size) {
      fprintf(stderr, "Error reading pot file: %s\n", strerror(errno));
      exit(-1);
    }
  } else {
    /* Allocate an empty string. */
    pot_file_data = calloc(1, sizeof(char));
    if (pot_file_data == NULL) {
      fprintf(stderr, "Failed to allocate buffer for pot file.\n");
      exit(-1);
    }
  }

  FCLOSE(f);

  /* Check if the second arg is a hash or a file containing hashes. */
  if (stat(av[2], &st) == 0)
    filename = av[2];
  else {
    single_hash = av[2];

    /* Ensure that hash is lowercase. */
    str_to_lowercase(single_hash);

    /* If this hash is already in the pot file, then there's nothing else to do. */
    if (pot_file_data && strstr(pot_file_data, single_hash)) {
      printf("Specified hash has already been cracked!  Check %s.\n", jtr_pot_filename);
      exit(0);
    }
  }

  if (filename) {
    FILE *f = fopen(filename, "rb");
    unsigned int previously_cracked = 0;


    if (f == NULL) {
      fprintf(stderr, "Error while opening file %s for reading: %s\n", filename, strerror(errno));
      goto err;
    }

    file_data = calloc(st.st_size + 1, sizeof(char));
    if (file_data == NULL) {
      fprintf(stderr, "Error while allocating buffer for hash file.\n");
      goto err;
    }

    if (fread(file_data, sizeof(char), st.st_size, f) != st.st_size) {
      fprintf(stderr, "Error while reading hash file: %s\n", strerror(errno));
      goto err;
    }

    FCLOSE(f);

    /* Count the number of newlines in the file so we know how large to make the
     * hash array. */
    for (i = 0; i < st.st_size; i++) {
      if (file_data[i] == '\n')
	max_num_hashes++;
    }
    max_num_hashes++;  /* In case the last line doesn't end with an LF. */

    num_colons = 0;
    for (i = 0; i < st.st_size; i++) {
      if (file_data[i] == ':')
        num_colons++;
      else if (file_data[i] == '\n')
        break;
    }

    if (num_colons == 0) {
      file_format = HASH_FILE_FORMAT_PLAIN;
      printf("Hash file contains plain hashes.\n");
    } else if (num_colons == 6) {
      file_format = HASH_FILE_FORMAT_PWDUMP;
      printf("Hash file is pwdump format.\n");
    } else {
      fprintf(stderr, "Error: hash file format is not recognized (number of colons in first line is %u, instead of 0 or 6).\n", num_colons);
      goto err;
    }

    usernames = calloc(max_num_hashes, sizeof(char *));
    hashes = calloc(max_num_hashes, sizeof(char *));
    if ((usernames == NULL) || (hashes == NULL)) {
      fprintf(stderr, "Error while allocating buffer for hashes.\n");
      goto err;
    }

    /* Tokenize the hash file by line.  Store each hash in the array. */
    num_hashes = 0;
    line = strtok(file_data, "\n");
    while (line && (num_hashes < max_num_hashes)) {

      /* Skip empty lines.  */
      if (strlen(line) > 0) {

	/* Skip previously-cracked hashes. */
	if (strstr(pot_file_data, line) != NULL)
	  previously_cracked++;
	else {
          /* If we're dealing with CRLF line endings, cut off the trailing CR. */
          if (line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = '\0';

          if (file_format == HASH_FILE_FORMAT_PLAIN) {
            /* Ensure that hash is lowercase. */
            str_to_lowercase(line);

            hashes[num_hashes] = strdup(line);
            if (hashes[num_hashes] == NULL) {
              fprintf(stderr, "Error while allocating buffer for hashes.\n");
              goto err;
            }
            num_hashes++;
          } else {  /* HASH_FILE_FORMAT_PWDUMP */
            char *line_copy = strdup(line);
            char *hash = NULL;
            unsigned int line_copy_len = strlen(line_copy);
            unsigned int hash_start = 0, hash_end = 0;


            /* Get the username from position zero until the first colon. */
            for (i = 0; i < line_copy_len; i++) {
              if (line_copy[i] == ':') {
                line_copy[i] = '\0';
                usernames[num_hashes] = strdup(line_copy);
                if (usernames[num_hashes] == NULL) {
                  fprintf(stderr, "Error while allocating buffer for usernames.\n");
                  goto err;
                }
                break;
              }
            }

            /* Find the start and end positions of the hash, based on the number of colons. */
            num_colons = 1;
            hash_start = 0;
            hash_end = 0;
            for (i = i + 1; i < line_copy_len; i++) {
              if (line_copy[i] == ':')
                num_colons++;

              if ((num_colons == 3) && (hash_start == 0))
                hash_start = i + 1;
              else if (num_colons == 4) {
                hash_end = i;
                break;
              }
            }

            if ((hash_start == 0) || (hash_end == 0)) {
              fprintf(stderr, "Error: failed to extract hash from line: [%s]\n", line);
              goto err;
            }

            *(line_copy + hash_end) = '\0';
            hash = line_copy + hash_start;
            /*printf("Found hash at %u:%u: [%s]\n", hash_start, hash_end, hash);*/

            /* Make sure the hash is 32 bytes. */
            if (strlen(hash) != 32) {
              fprintf(stderr, "Error: hash is length %u instead of 32: [%s]\n", (unsigned int)strlen(hash), hash);
              goto err;
            }

            str_to_lowercase(hash);  /* Ensure hash is lowercase. */

            if (strstr(pot_file_data, hash) != NULL) {
              previously_cracked++;
            } else {
              hashes[num_hashes] = strdup(hash);
              if (hashes[num_hashes] == NULL) {
                fprintf(stderr, "Error while allocating buffer for hashes.\n");
                goto err;
              }
              num_hashes++;
            }
            FREE(line_copy);

          }
        }
	line = strtok(NULL, "\n");
      }
    }

    FREE(file_data);

    if (num_hashes == 0) {
      printf("All hashes have already been cracked!  Check %s.\n", jtr_pot_filename);
      exit(0);
    } else {
      printf("Loaded %u of %u uncracked hashes from %s.\n", num_hashes, num_hashes + previously_cracked, filename);  fflush(stdout);
    }

  } else { /* A single hash was provided. */
    usernames = calloc(1, sizeof(char *));
    hashes = calloc(1, sizeof(char *));
    if ((usernames == NULL) || (hashes == NULL)) {
      fprintf(stderr, "Error while allocating buffer for hashes.\n");
      goto err;
    }

    usernames[0] = NULL;
    hashes[0] = strdup(single_hash);
    num_hashes = 1;
  }

  /* We're done checking the pot file for previously-cracked hashes. */
  FREE(pot_file_data);

  /* Enumerate every distinct table configuration present in the directory.
   * Each configuration requires its own precomputation pass because the
   * endpoints depend on the charset, chain length, and table index. */
  config_group *cg_head = NULL;
  collect_config_groups(rt_dir, &cg_head);
  if (cg_head == NULL) {
    fprintf(stderr, "Failed to find any valid rainbow table files in %s (and/or its sub-directories).\n", rt_dir);
    exit(-1);
  }

  /* Use the first config group's hash type for hash format validation. */
  if (cg_head->params.hash_type == HASH_NTLM) {
    for (i = 0; i < num_hashes; i++) {
      if (strlen(hashes[i]) != 32) {
        fprintf(stderr, "Error: invalid NTLM hash (length is not 32!): %s\n", hashes[i]);
        exit(-1);
      }
    }
  }

  /* Resolve the active NetNTLMv1 server challenge from the loaded tables.
   * Each config group carries the challenge parsed from its filename (default
   * 1122334455667788 when no -chal suffix is present).  Validate that all
   * loaded tables agree, honor/validate any user-supplied --challenge, then
   * adopt the result and push it to the CPU hashing path.  Only applies to
   * NetNTLMv1 tables; NTLM/MD5 runs never touch g_netntlmv1_challenge. */
  if (cg_head->params.hash_type == HASH_NETNTLMV1) {
    config_group *first_cg = cg_head;
    const unsigned char *table_challenge = first_cg->params.challenge;

    /* All loaded tables must carry the same challenge. */
    for (config_group *cg = cg_head->next; cg != NULL; cg = cg->next) {
      if (memcmp(cg->params.challenge, table_challenge, 8) != 0) {
        char a[17] = {0}, b[17] = {0};
        format_challenge_hex(table_challenge, a);
        format_challenge_hex(cg->params.challenge, b);
        fprintf(stderr, "Error: loaded tables carry conflicting NetNTLMv1 challenges "
                "('%s' for charset '%s' vs '%s' for charset '%s').  Look up one challenge at a time.\n",
                a, first_cg->params.charset_name, b, cg->params.charset_name);
        exit(-1);
      }
    }

    if (g_challenge_set && memcmp(g_challenge, table_challenge, 8) != 0) {
      char a[17] = {0}, b[17] = {0};
      format_challenge_hex(g_challenge, a);
      format_challenge_hex(table_challenge, b);
      fprintf(stderr, "Error: --challenge %s does not match the loaded tables' challenge %s (charset '%s').\n",
              a, b, first_cg->params.charset_name);
      exit(-1);
    }

    /* Adopt the tables' challenge. */
    memcpy(g_challenge, table_challenge, 8);
    g_challenge_set = 1;
    set_netntlmv1_challenge(g_challenge);
  }

  /* Issue a warning if more than 5,000 hashes were provided, as rainbow tables may
   * start to become not as efficient as brute-force. */
  if (num_hashes > 5000) {
    printf("\n\n\n\t!! WARNING !!\n\nA large group of hashes was provided (%u).  In general, rainbow tables are only effective to use for small numbers of hashes because there is a pre-computation step that must be done on *each hash*; eventually this pre-computation cost becomes high enough that brute-force would be a better strategy.  The point at which this happens depends on your specific GPU hardware.\n\nFor example, suppose the pre-computation step takes 2.8 seconds per hash, and brute-forcing takes 16 hours (57,600 seconds).  Not counting search time nor false alarm checking, the point at which brute-forcing becomes more efficient than rainbow tables is: 57,600 / 2.8 = ~20,571 hashes.  Trying to crack more than this number of hashes is clearly less effective than brute-force.\n\nPay attention to the pre-computation times below, and compare with the reported estimate that hashcat gives after a few minutes for brute-forcing 8-character NTLM (hint: ./hashcat -m 1000 -a 3 -w 3 -O ffffffffffffffffffffffffffffffff ?a?a?a?a?a?a?a?a).\n\n\n\n", num_hashes);  fflush(stdout);
  }

  args = calloc(num_devices, sizeof(thread_args));
  if (args == NULL) {
    fprintf(stderr, "Error while creating thread arg array.\n");
    goto err;
  }

  /* If --markov was requested, load the model once before spawning threads. */
  if (use_markov) {
    if (markov_load(markov_path, &g_markov) != 0) {
      fprintf(stderr, "Error: failed to load Markov model from '%s'\n", markov_path);
      goto err;
    }
    printf("Loaded Markov model from %s (charset_len=%u).\n", markov_path, g_markov.charset_len);
    fflush(stdout);
  }

  /* Set per-GPU constant fields that don't change across config groups. */
  for (i = 0; i < num_devices; i++) {
    args[i].username       = NULL;  /* Filled in per-hash below. */
    args[i].hash           = NULL;  /* Filled in per-hash below. */
    args[i].total_devices  = num_devices;
    args[i].gpu.device_number = i;
    args[i].gpu.device     = devices[i];
    get_device_uint(args[i].gpu.device, CL_DEVICE_MAX_COMPUTE_UNITS, &(args[i].gpu.num_work_units));
    args[i].use_markov     = use_markov;
    if (use_markov) {
      args[i].sorted_pos0       = g_markov.sorted_pos0;
      args[i].sorted_bigram     = g_markov.sorted_bigram;
      args[i].markov_charset_len = g_markov.charset_len;
      args[i].markov_max_positions = g_markov.max_positions;
    }
  }

  /* Count config groups for progress reporting. */
  unsigned int num_config_groups = 0;
  for (config_group *cg = cg_head; cg != NULL; cg = cg->next)
    num_config_groups++;

  unsigned int config_group_num = 0;
  for (config_group *cg = cg_head; cg != NULL; cg = cg->next) {
    unsigned int num_uncracked = 0;

    config_group_num++;

    /* Count remaining uncracked hashes. */
    ppi_cur = ppi_head;
    while (ppi_cur != NULL) {
      if (ppi_cur->plaintext == NULL)
        num_uncracked++;
      ppi_cur = ppi_cur->next;
    }
    /* If ppi_head is still NULL (first pass) count all hashes as uncracked. */
    if (ppi_head == NULL)
      num_uncracked = num_hashes;

    if (num_uncracked == 0) {
      printf("All hashes cracked.  Skipping remaining config groups.\n");  fflush(stdout);
      break;
    }

    /* Skip config groups whose hash type doesn't match the input hashes.
     * The target hash type is taken from the first config group. */
    if (cg->params.hash_type != cg_head->params.hash_type)
      continue;

    /* Skip config groups that have no tables in the directory (can happen if
     * a table was removed between enumeration and search). */
    unsigned int config_table_count = count_tables_for_config(rt_dir, &cg->params);
    if (config_table_count == 0)
      continue;

    if (num_config_groups > 1)
      printf("\n[Config group %u of %u] charset=%s len=%u-%u table_index=%u chain_len=%u\n",
             config_group_num, num_config_groups,
             cg->params.charset_name,
             cg->params.plaintext_len_min, cg->params.plaintext_len_max,
             cg->params.table_index, cg->params.chain_len);

    /* Apply this config group's parameters to args. */
    setup_args_for_config(args, num_devices, &cg->params);

    /* Reset kernel-selection message flags so the right message prints for
     * each config group. */
    printed_precompute_optimized_message = 0;
    printed_false_alarm_optimized_message = 0;

    /* Streaming lookup: sliding-window per-table load + batched GPU
     * precompute + baseline-style search loop.  Carved-down replacement
     * for pipelined_lookup; same wall-time, dramatically lower RSS. */
    start_timer(&precompute_start_time);
    /* Reset the precompute progress counters for this config group so the ETA
     * (print_eta_precompute) has a valid total to divide against.  Without a
     * non-zero total the guard in print_eta_precompute fails and it always
     * reports "Unknown".  num_hashes is the upper bound on hashes precomputed
     * here, matching the "(at most)" wording of the estimate. */
    num_hashes_precomputed = 0;
    num_hashes_precomputed_total = num_hashes;
    streaming_lookup(rt_dir, &cg->params, num_devices, args,
                     hashes, usernames, num_hashes, &ppi_head);
    time_precomp += get_elapsed(&precompute_start_time);
  }

  free_config_groups(&cg_head);

  seconds_to_human_time(time_precomp_str, sizeof(time_precomp_str), time_precomp);
  seconds_to_human_time(time_io_str, sizeof(time_io_str), time_io);
  seconds_to_human_time(time_searching_str, sizeof(time_searching_str), time_searching);
  seconds_to_human_time(time_falsealarms_str, sizeof(time_falsealarms_str), time_falsealarms);
  seconds_to_human_time(time_total_str, sizeof(time_total_str), time_precomp + /*time_io +*/ time_searching + time_falsealarms);
  seconds_to_human_time(time_per_table_str, sizeof(time_per_table_str), (double)(time_precomp + time_io + time_searching + time_falsealarms) / (double)num_tables_processed);

  printf("\n\n        %sRAINBOW CRACKALACK LOOKUP REPORT%s\n\n", WHITEB, CLR);

  if (num_cracked == 0)
    printf("\nNo hashes were cracked.  :(\n\n\n");
  else {
    printf(" %s* Crack Summary *%s\n\n", WHITEB, CLR);
    printf("   Of the %u hashes loaded, %u were cracked, or %.2f%%.\n\n", num_hashes, num_cracked, ((double)num_cracked / (double)num_hashes) * 100);

    printf(" Results\n -------\n%s", GREENB);
    ppi_cur = ppi_head;
    while(ppi_cur != NULL) {
      if (ppi_cur->plaintext != NULL) {
	  printf(" %s  %s\n", (ppi_cur->username != NULL) ? ppi_cur->username : ppi_cur->hash, ppi_cur->plaintext);
      }

      ppi_cur = ppi_cur->next;
    }
    printf("%s -------\n\n", CLR);
    printf("%s Results have been written in JTR format to:     %s\n", WHITEB, jtr_pot_filename);
    printf(" Results have been written in hashcat format to: %s%s\n\n\n", hashcat_pot_filename, CLR);
  }

  printf(" %s* Time Summary *%s\n\n      Precomputation: %s\n      I/O (parallel): %s\n           Searching: %s\n  False alarm checks: %s\n\n               Total: %s\n\n\n", WHITEB, CLR, time_precomp_str, time_io_str, time_searching_str, time_falsealarms_str, time_total_str);

  printf(" %s* Statistics *%s\n\n          Number of tables processed: %u\n              Number of false alarms: %" QUOTE PRIu64"\n          Number of chains processed: %" QUOTE PRIu64"\n\n                Time spent per table: %s\n     False alarms checked per second: %" QUOTE ".1f\n\n         False alarms per no. chains: %.5f%%\n  Successful cracks per false alarms: %.5f%%\n  Successful cracks per total chains: %.8f%%\n\n\n", WHITEB, CLR, num_tables_processed, num_falsealarms, num_chains_processed, time_per_table_str, (double)num_falsealarms / time_falsealarms, ((double)num_falsealarms / (double)num_chains_processed) * 100.0, ((double)num_cracked / (double)num_falsealarms) * 100.0, ((double)num_cracked / (double)num_chains_processed) * 100.0);


  free_precomputed_and_potential_indices(&ppi_head);
  free_loaded_hashes(usernames, hashes);
  FREE(args);
  if (use_markov)
    markov_free(&g_markov);
  pthread_barrier_destroy(&barrier);

  if (g_bloom_agg.tables > 0) {
    uint64_t denom = (g_bloom_agg.queries > g_bloom_agg.confirmed)
                     ? (g_bloom_agg.queries - g_bloom_agg.confirmed) : 0;
    uint64_t fp    = (g_bloom_agg.passes > g_bloom_agg.confirmed)
                     ? (g_bloom_agg.passes - g_bloom_agg.confirmed) : 0;
    double agg_fpr = (denom > 0) ? (100.0 * (double)fp / (double)denom) : 0.0;
    fprintf(stderr, "[bloom] (aggregate, %u tables) queries=%llu passes=%llu confirmed=%llu observed_fpr=%.4f%%\n",
            g_bloom_agg.tables,
            (unsigned long long)g_bloom_agg.queries,
            (unsigned long long)g_bloom_agg.passes,
            (unsigned long long)g_bloom_agg.confirmed,
            agg_fpr);
  }

  return 0;

 err:
  FCLOSE(f);
  FREE(file_data);

  free_precomputed_and_potential_indices(&ppi_head);
  free_loaded_hashes(usernames, hashes);
  FREE(args);
  if (use_markov)
    markov_free(&g_markov);
  pthread_barrier_destroy(&barrier);
  return -1;
}
