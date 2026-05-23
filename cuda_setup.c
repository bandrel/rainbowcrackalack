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

/* CUDA has no platform concept.  We expose a single dummy platform
 * (index 0) so the existing 2-arg consumer API works unchanged. */
void get_platforms_and_devices(int disable_platform,
                               gpu_uint platforms_buffer_size, gpu_platform *platforms, gpu_uint *num_platforms,
                               gpu_uint devices_buffer_size,   gpu_device   *devices,   gpu_uint *num_devices,
                               unsigned int verbose) {
  (void)disable_platform;  /* CUDA: only one "platform"; flag is a no-op */
  (void)verbose;

  CUresult res = cuInit(0);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuInit failed: %s\n", err ? err : "(unknown)");
    exit(-1);
  }

  int device_count = 0;
  res = cuDeviceGetCount(&device_count);
  if (res != CUDA_SUCCESS || device_count == 0) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuDeviceGetCount failed or no devices: %s\n", err ? err : "(none)");
    exit(-1);
  }

  if (platforms_buffer_size >= 1 && platforms != NULL)
    platforms[0] = 0;
  if (num_platforms != NULL)
    *num_platforms = 1;

  unsigned int n = (unsigned int)device_count;
  if (n > devices_buffer_size) n = devices_buffer_size;
  for (unsigned int i = 0; i < n; i++) {
    CUdevice dev;
    res = cuDeviceGet(&dev, (int)i);
    if (res != CUDA_SUCCESS) {
      const char *err = NULL;
      cuGetErrorString(res, &err);
      fprintf(stderr, "cuDeviceGet(%u) failed: %s\n", i, err ? err : "(unknown)");
      exit(-1);
    }
    devices[i] = dev;
  }
  if (num_devices != NULL)
    *num_devices = n;
}

void get_device_str(gpu_device device, unsigned int param, char *buf, int buf_len) {
  switch (param) {
    case GPU_DEVICE_NAME:
      cuDeviceGetName(buf, buf_len, device);
      break;
    case GPU_DEVICE_VENDOR:
      /* CUDA doesn't expose a vendor string; force "NVIDIA Corporation". */
      snprintf(buf, buf_len, "NVIDIA Corporation");
      break;
    case GPU_DEVICE_VERSION:
    case GPU_DRIVER_VERSION: {
      int driver = 0;
      cuDriverGetVersion(&driver);
      snprintf(buf, buf_len, "CUDA %d.%d", driver / 1000, (driver % 100) / 10);
      break;
    }
    default:
      buf[0] = '\0';
  }
}

void get_device_bool(gpu_device device, unsigned int param, gpu_bool *b) {
  (void)device;
  if (param == GPU_DEVICE_AVAILABLE) { *b = GPU_TRUE; return; }
  *b = GPU_FALSE;
}

void get_device_uint(gpu_device device, unsigned int param, gpu_uint *u) {
  int v = 0;
  switch (param) {
    case GPU_DEVICE_MAX_COMPUTE_UNITS:
      cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, device);
      break;
    case GPU_DEVICE_MAX_WORK_GROUP_SIZE:
      cuDeviceGetAttribute(&v, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, device);
      break;
    default:
      v = 0;
  }
  *u = (gpu_uint)v;
}

void get_device_ulong(gpu_device device, unsigned int param, gpu_ulong *ul) {
  size_t total = 0;
  switch (param) {
    case GPU_DEVICE_GLOBAL_MEM_SIZE:
      cuDeviceTotalMem(&total, device);
      break;
    default:
      total = 0;
  }
  *ul = (gpu_ulong)total;
}

void print_device_info(gpu_device *devices, gpu_uint num_devices) {
  /* The lookup binary's existing startup banner prints platform/device
   * info by calling get_device_str/uint/ulong directly.  This function
   * is called from a few utility binaries that want a one-shot dump. */
  for (gpu_uint i = 0; i < num_devices; i++) {
    char buf[256] = {0};
    gpu_uint cu = 0, max_wg = 0;
    gpu_ulong mem = 0;
    get_device_str(devices[i], GPU_DEVICE_NAME, buf, sizeof(buf));
    get_device_uint(devices[i], GPU_DEVICE_MAX_COMPUTE_UNITS, &cu);
    get_device_uint(devices[i], GPU_DEVICE_MAX_WORK_GROUP_SIZE, &max_wg);
    get_device_ulong(devices[i], GPU_DEVICE_GLOBAL_MEM_SIZE, &mem);
    printf("Device #%u: %s (SMs=%u, max-threads/block=%u, mem=%llu MB)\n",
           i, buf, cu, max_wg, (unsigned long long)(mem / (1024ULL * 1024ULL)));
  }
}
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
