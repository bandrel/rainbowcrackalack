/*
 * Rainbow Crackalack: cuda_setup.c
 *
 * CUDA Driver API backend for Linux.  Mirrors the function surface
 * of metal_setup.m / opencl_setup.c.  NVRTC is used to compile
 * kernels from .cu source at runtime.
 *
 * All functions return error or print "TODO" and exit until the
 * implementation lands in later tasks.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cuda.h>
#include <nvrtc.h>

#include "gpu_backend.h"

#define CUDA_TODO(_name) do { \
  fprintf(stderr, "cuda_setup: TODO " _name "\n"); \
  exit(-1); \
} while (0)

/* CUDA has no equivalent context error callback.  Provide a no-op
 * definition so CLCREATECONTEXT call sites in consumer code (which
 * are still OpenCL-shaped at this point) don't fail to link. */
void context_callback(const char *errinfo, const void *private_info, size_t cb, void *user_data) {
  (void)errinfo; (void)private_info; (void)cb; (void)user_data;
}

void get_platforms_and_devices(int disable_platform,
                               gpu_uint platforms_buffer_size, gpu_platform *platforms, gpu_uint *num_platforms,
                               gpu_uint devices_buffer_size,   gpu_device   *devices,   gpu_uint *num_devices,
                               unsigned int verbose) {
  (void)disable_platform; (void)platforms_buffer_size; (void)platforms;
  (void)num_platforms;    (void)devices_buffer_size;   (void)devices;
  (void)num_devices;      (void)verbose;
  CUDA_TODO("get_platforms_and_devices");
}

void get_device_bool(gpu_device d, unsigned int p, gpu_bool *b)      { (void)d; (void)p; (void)b;  CUDA_TODO("get_device_bool"); }
void get_device_str (gpu_device d, unsigned int p, char *buf, int n) { (void)d; (void)p; (void)buf; (void)n; CUDA_TODO("get_device_str"); }
void get_device_uint(gpu_device d, unsigned int p, gpu_uint *u)      { (void)d; (void)p; (void)u;  CUDA_TODO("get_device_uint"); }
void get_device_ulong(gpu_device d, unsigned int p, gpu_ulong *ul)   { (void)d; (void)p; (void)ul; CUDA_TODO("get_device_ulong"); }
void print_device_info(gpu_device *devices, gpu_uint n)              { (void)devices; (void)n;     CUDA_TODO("print_device_info"); }
void gpu_release_device(gpu_device d)                                 { (void)d; /* no-op: CUDA does not require release */ }

void load_kernel(gpu_context context, gpu_uint num_devices, const gpu_device *devices,
                 const char *path, const char *kernel_name,
                 gpu_program *program, gpu_kernel *kernel, unsigned int hash_type) {
  (void)context; (void)num_devices; (void)devices; (void)path; (void)kernel_name;
  (void)program; (void)kernel;      (void)hash_type;
  CUDA_TODO("load_kernel");
}

gpu_context gpu_create_context(gpu_device d) { (void)d; CUDA_TODO("gpu_create_context"); return NULL; }
gpu_queue   gpu_create_queue(gpu_context c, gpu_device d) { (void)c; (void)d; CUDA_TODO("gpu_create_queue"); return NULL; }
gpu_buffer  gpu_create_buffer(gpu_context c, int flags, size_t size) { (void)c; (void)flags; (void)size; CUDA_TODO("gpu_create_buffer"); return 0; }
gpu_buffer  gpu_create_and_fill_buffer(gpu_context c, int flags, size_t size, const void *data) { (void)c; (void)flags; (void)size; (void)data; CUDA_TODO("gpu_create_and_fill_buffer"); return 0; }
int gpu_write_buffer(gpu_queue q, gpu_buffer b, size_t n, const void *p) { (void)q; (void)b; (void)n; (void)p; CUDA_TODO("gpu_write_buffer"); return -1; }
int gpu_read_buffer (gpu_queue q, gpu_buffer b, size_t n,       void *p) { (void)q; (void)b; (void)n; (void)p; CUDA_TODO("gpu_read_buffer");  return -1; }
void gpu_release_buffer (gpu_buffer b)   { (void)b; CUDA_TODO("gpu_release_buffer"); }
void gpu_release_queue  (gpu_queue q)    { (void)q; CUDA_TODO("gpu_release_queue"); }
void gpu_release_context(gpu_context c)  { (void)c; CUDA_TODO("gpu_release_context"); }
void gpu_release_kernel (gpu_kernel k)   { (void)k; /* CUfunction is owned by its module */ }
void gpu_release_program(gpu_program p)  { (void)p; CUDA_TODO("gpu_release_program"); }

int gpu_set_kernel_arg(gpu_kernel k, unsigned int idx, size_t size, const void *value) { (void)k; (void)idx; (void)size; (void)value; CUDA_TODO("gpu_set_kernel_arg"); return -1; }
int gpu_enqueue_kernel(gpu_queue q, gpu_kernel k, unsigned int dim, size_t *gws) { (void)q; (void)k; (void)dim; (void)gws; CUDA_TODO("gpu_enqueue_kernel"); return -1; }
int gpu_flush (gpu_queue q) { (void)q; CUDA_TODO("gpu_flush");  return -1; }
int gpu_finish(gpu_queue q) { (void)q; CUDA_TODO("gpu_finish"); return -1; }
int gpu_get_kernel_work_group_info(gpu_kernel k, gpu_device d, unsigned int p, size_t sz, void *val) { (void)k; (void)d; (void)p; (void)sz; (void)val; CUDA_TODO("gpu_get_kernel_work_group_info"); return -1; }

/* Per-thread context lifecycle.  Worker threads call attach before any
 * CUDA call and detach after.  No-op on OpenCL/Metal backends. */
void gpu_thread_attach(void) { CUDA_TODO("gpu_thread_attach"); }
void gpu_thread_detach(void) { CUDA_TODO("gpu_thread_detach"); }
