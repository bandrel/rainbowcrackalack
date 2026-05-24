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

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cuda.h>
#include <nvrtc.h>

#include "gpu_backend.h"

#define CUDA_TODO(_name) do { \
  fprintf(stderr, "cuda_setup: TODO " _name "\n"); \
  exit(-1); \
} while (0)

/* Track which CUcontext to push.  Set once by main thread after
 * gpu_create_context.  All worker threads push this. */
static CUcontext g_default_context = NULL;

/* Per-kernel arg table.  CUfunction is opaque; we key by pointer
 * value into a small linear list.  Each kernel call site can have
 * at most CUDA_MAX_KERNEL_ARGS args.  Keep slots indexed by the
 * arg_index passed to gpu_set_kernel_arg. */
#define CUDA_MAX_KERNEL_ARGS    32
#define CUDA_MAX_TRACKED_KERNELS 64

typedef struct {
  CUfunction kernel;
  /* Storage for arg VALUES (not the host buffer's address).
   * For CUdeviceptr args, value is the CUdeviceptr itself, stored in
   * cuda_arg_storage[slot].  kernelParams holds pointers INTO this
   * storage array. */
  CUdeviceptr storage[CUDA_MAX_KERNEL_ARGS];
  void       *params [CUDA_MAX_KERNEL_ARGS];
  unsigned int max_set_index;  /* highest arg_index set + 1 */
} cuda_kernel_args;

static cuda_kernel_args g_cuda_arg_tables[CUDA_MAX_TRACKED_KERNELS];
static unsigned int     g_cuda_arg_tables_used = 0;

static cuda_kernel_args *cuda_get_arg_table(CUfunction k) {
  for (unsigned int i = 0; i < g_cuda_arg_tables_used; i++)
    if (g_cuda_arg_tables[i].kernel == k) return &g_cuda_arg_tables[i];
  if (g_cuda_arg_tables_used >= CUDA_MAX_TRACKED_KERNELS) {
    fprintf(stderr, "cuda_setup: too many tracked kernels (max %d)\n",
            CUDA_MAX_TRACKED_KERNELS);
    exit(-1);
  }
  cuda_kernel_args *t = &g_cuda_arg_tables[g_cuda_arg_tables_used++];
  memset(t, 0, sizeof(*t));
  t->kernel = k;
  return t;
}

/* Forward declaration — defined just before gpu_create_context. */
static void cuda_remember_context(CUcontext c);

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

/* Read entire file into a malloc'd, NUL-terminated buffer.  Returns NULL on error. */
static char *cuda_read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cuda_setup: failed to open '%s': %s\n", path, strerror(errno));
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0) { fclose(f); return NULL; }
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
  buf[n] = '\0';
  fclose(f);
  return buf;
}

/* Recursively resolve #include "foo.cu" / "foo.cl" lines by inlining the
 * referenced file's contents.  `dir` is the directory of the file being
 * resolved (where included files are looked up).  Returns a malloc'd
 * NUL-terminated buffer with all includes inlined. */
static char *cuda_resolve_includes(const char *src, const char *dir) {
  /* Quick-and-dirty: scan for lines starting with `#include "`, inline,
   * repeat until no more includes remain.  Adequate for the project's
   * small, well-formed kernel sources. */
  size_t cap = strlen(src) + 1;
  char *out = malloc(cap);
  if (!out) return NULL;
  memcpy(out, src, cap);

  for (;;) {
    char *inc = strstr(out, "#include \"");
    if (!inc) break;
    /* Find newline ending the directive. */
    char *nl = strchr(inc, '\n');
    if (!nl) { fprintf(stderr, "cuda_setup: malformed #include\n"); free(out); return NULL; }
    /* Extract the quoted filename. */
    char *q1 = inc + strlen("#include \"");
    char *q2 = strchr(q1, '\"');
    if (!q2 || q2 > nl) { fprintf(stderr, "cuda_setup: malformed #include\n"); free(out); return NULL; }
    char inc_name[256];
    size_t name_len = (size_t)(q2 - q1);
    if (name_len >= sizeof(inc_name)) { fprintf(stderr, "cuda_setup: #include name too long\n"); free(out); return NULL; }
    memcpy(inc_name, q1, name_len);
    inc_name[name_len] = '\0';

    char inc_path[512];
    snprintf(inc_path, sizeof(inc_path), "%s/%s", dir, inc_name);
    char *inc_src = cuda_read_file(inc_path);
    if (!inc_src) { free(out); return NULL; }

    /* Replace the #include line with the inlined source. */
    size_t before_len = (size_t)(inc - out);
    size_t after_len  = strlen(nl + 1);
    size_t inc_len    = strlen(inc_src);
    size_t new_size   = before_len + inc_len + 1 + after_len + 1;
    char *new_out = malloc(new_size);
    if (!new_out) { free(out); free(inc_src); return NULL; }
    memcpy(new_out, out, before_len);
    memcpy(new_out + before_len, inc_src, inc_len);
    new_out[before_len + inc_len] = '\n';
    memcpy(new_out + before_len + inc_len + 1, nl + 1, after_len + 1);

    free(out);
    free(inc_src);
    out = new_out;
  }

  return out;
}

static const char *cuda_dirname(const char *path, char *buf, size_t buf_size) {
  const char *slash = strrchr(path, '/');
  if (!slash) { snprintf(buf, buf_size, "."); return buf; }
  size_t n = (size_t)(slash - path);
  if (n >= buf_size) n = buf_size - 1;
  memcpy(buf, path, n);
  buf[n] = '\0';
  return buf;
}

void load_kernel(gpu_context context, gpu_uint num_devices, const gpu_device *devices,
                 const char *path, const char *kernel_name,
                 gpu_program *program, gpu_kernel *kernel, unsigned int hash_type) {
  (void)context; (void)num_devices; (void)hash_type;

  /* Read kernel source and resolve includes. */
  char *src = cuda_read_file(path);
  if (!src) exit(-1);

  char dir[512];
  cuda_dirname(path, dir, sizeof(dir));
  char *full = cuda_resolve_includes(src, dir);
  free(src);
  if (!full) exit(-1);

  /* Query compute capability of the first device for arch targeting. */
  int cc_major = 0, cc_minor = 0;
  cuDeviceGetAttribute(&cc_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, devices[0]);
  cuDeviceGetAttribute(&cc_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, devices[0]);
  char arch_opt[64];
  snprintf(arch_opt, sizeof(arch_opt), "--gpu-architecture=compute_%d%d", cc_major, cc_minor);

  /* NVRTC compile. */
  nvrtcProgram prog;
  nvrtcResult nres = nvrtcCreateProgram(&prog, full, path, 0, NULL, NULL);
  if (nres != NVRTC_SUCCESS) {
    fprintf(stderr, "nvrtcCreateProgram failed: %s\n", nvrtcGetErrorString(nres));
    free(full);
    exit(-1);
  }
  const char *options[] = { arch_opt, "--use_fast_math" };
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  nres = nvrtcCompileProgram(prog, 2, options);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double compile_secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

  if (nres != NVRTC_SUCCESS) {
    fprintf(stderr, "nvrtcCompileProgram failed: %s\n", nvrtcGetErrorString(nres));
    size_t log_size = 0;
    nvrtcGetProgramLogSize(prog, &log_size);
    if (log_size > 0) {
      char *log = malloc(log_size);
      if (log) {
        nvrtcGetProgramLog(prog, log);
        fprintf(stderr, "NVRTC log:\n%s\n", log);
        free(log);
      }
    }
    nvrtcDestroyProgram(&prog);
    free(full);
    exit(-1);
  }

  /* Retrieve PTX. */
  size_t ptx_size = 0;
  nvrtcGetPTXSize(prog, &ptx_size);
  char *ptx = malloc(ptx_size);
  nvrtcGetPTX(prog, ptx);
  fprintf(stderr, "  [cuda] %s: compiled in %.2fs, PTX size %zu bytes (arch=compute_%d%d)\n",
          kernel_name, compile_secs, ptx_size, cc_major, cc_minor);
  nvrtcDestroyProgram(&prog);
  free(full);

  /* Load PTX as a CUmodule and look up the entry function. */
  CUmodule mod;
  CUresult cres = cuModuleLoadData(&mod, ptx);
  free(ptx);
  if (cres != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(cres, &err);
    fprintf(stderr, "cuModuleLoadData failed for %s: %s\n", kernel_name, err ? err : "(unknown)");
    exit(-1);
  }

  CUfunction fn;
  cres = cuModuleGetFunction(&fn, mod, kernel_name);
  if (cres != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(cres, &err);
    fprintf(stderr, "cuModuleGetFunction(%s) failed: %s\n", kernel_name, err ? err : "(unknown)");
    cuModuleUnload(mod);
    exit(-1);
  }

  *program = mod;
  *kernel  = fn;
}

/* Called by gpu_create_context to record the context for worker threads. */
static void cuda_remember_context(CUcontext c) {
  g_default_context = c;
}

gpu_context gpu_create_context(gpu_device device) {
  CUcontext ctx;
  CUresult res = cuCtxCreate(&ctx, 0, device);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuCtxCreate failed: %s\n", err ? err : "(unknown)");
    return NULL;
  }
  cuda_remember_context(ctx);
  return ctx;
}

gpu_queue gpu_create_queue(gpu_context context, gpu_device device) {
  (void)device;
  /* Make sure the requested context is current on this thread. */
  CUresult res = cuCtxSetCurrent(context);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuCtxSetCurrent failed: %s\n", err ? err : "(unknown)");
    return NULL;
  }
  CUstream stream;
  res = cuStreamCreate(&stream, CU_STREAM_DEFAULT);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuStreamCreate failed: %s\n", err ? err : "(unknown)");
    return NULL;
  }
  return stream;
}

gpu_buffer gpu_create_buffer(gpu_context context, int flags, size_t size) {
  (void)context;  /* not needed; context is implicit on current thread */
  (void)flags;    /* CUDA doesn't distinguish RO/WO/RW at allocation time */
  CUdeviceptr dptr = 0;
  CUresult res = cuMemAlloc(&dptr, size);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuMemAlloc(%zu) failed: %s\n", size, err ? err : "(unknown)");
    return 0;
  }
  return dptr;
}

gpu_buffer gpu_create_and_fill_buffer(gpu_context context, int flags, size_t size, const void *data) {
  gpu_buffer buf = gpu_create_buffer(context, flags, size);
  if (buf == 0) return 0;
  if (data != NULL && size > 0) {
    CUresult res = cuMemcpyHtoD(buf, data, size);
    if (res != CUDA_SUCCESS) {
      const char *err = NULL;
      cuGetErrorString(res, &err);
      fprintf(stderr, "cuMemcpyHtoD failed: %s\n", err ? err : "(unknown)");
      cuMemFree(buf);
      return 0;
    }
  }
  return buf;
}

int gpu_write_buffer(gpu_queue queue, gpu_buffer buf, size_t size, const void *ptr) {
  (void)queue;  /* default stream is synchronous enough; FA path uses one stream per device */
  CUresult res = cuMemcpyHtoD(buf, ptr, size);
  return (res == CUDA_SUCCESS) ? 0 : -1;
}

int gpu_read_buffer(gpu_queue queue, gpu_buffer buf, size_t size, void *ptr) {
  (void)queue;
  CUresult res = cuMemcpyDtoH(ptr, buf, size);
  return (res == CUDA_SUCCESS) ? 0 : -1;
}

void gpu_release_buffer(gpu_buffer buf)  { if (buf != 0) cuMemFree(buf); }
void gpu_release_queue(gpu_queue q)      { if (q) cuStreamDestroy(q); }
void gpu_release_context(gpu_context c)  { if (c) cuCtxDestroy(c); }
void gpu_release_kernel (gpu_kernel k)   { (void)k; /* CUfunction is owned by its module */ }
void gpu_release_program(gpu_program p)  { if (p) cuModuleUnload(p); }

int gpu_set_kernel_arg(gpu_kernel k, unsigned int idx, size_t size, const void *value) {
  if (idx >= CUDA_MAX_KERNEL_ARGS) {
    fprintf(stderr, "gpu_set_kernel_arg: index %u exceeds max %d\n",
            idx, CUDA_MAX_KERNEL_ARGS);
    return -1;
  }
  cuda_kernel_args *t = cuda_get_arg_table(k);
  /* All our args are pointer-sized (CUdeviceptr).  Store the value
   * (which the caller hands us as &buffer) into storage[idx] and
   * point params[idx] at that storage slot. */
  if (size != sizeof(CUdeviceptr)) {
    fprintf(stderr, "gpu_set_kernel_arg: unsupported arg size %zu (only ptr-sized supported)\n", size);
    return -1;
  }
  t->storage[idx] = *(const CUdeviceptr *)value;
  t->params[idx]  = &t->storage[idx];
  if (idx + 1 > t->max_set_index) t->max_set_index = idx + 1;
  return 0;
}

int gpu_enqueue_kernel(gpu_queue q, gpu_kernel k, unsigned int dim, size_t *gws) {
  (void)dim;  /* always 1 in this codebase */
  cuda_kernel_args *t = cuda_get_arg_table(k);

  /* Block size choice: match OpenCL's CLRUNKERNEL local-work-size of 256.
   * Grid size = ceil(gws / block_size). */
  unsigned int block_size = 256;
  unsigned int grid_size = (unsigned int)((gws[0] + block_size - 1) / block_size);
  if (grid_size == 0) grid_size = 1;

  CUresult res = cuLaunchKernel(k,
                                grid_size, 1, 1,
                                block_size, 1, 1,
                                /*sharedMemBytes=*/ 0,
                                q,
                                t->params,
                                NULL);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuLaunchKernel failed: %s\n", err ? err : "(unknown)");
    return -1;
  }
  return 0;
}

int gpu_flush(gpu_queue q) {
  /* CUDA streams don't have an explicit flush; cuStreamSynchronize
   * is the closest equivalent and matches the OpenCL CLFLUSH semantics
   * used by callers (which then call CLWAIT immediately after). */
  (void)q;
  return 0;
}

int gpu_finish(gpu_queue q) {
  CUresult res = cuStreamSynchronize(q);
  return (res == CUDA_SUCCESS) ? 0 : -1;
}

int gpu_get_kernel_work_group_info(gpu_kernel k, gpu_device d, unsigned int param, size_t param_size, void *value) {
  (void)d;
  int v = 0;
  CUresult res = CUDA_SUCCESS;
  switch (param) {
    case GPU_KERNEL_WORK_GROUP_SIZE:
      res = cuFuncGetAttribute(&v, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, k);
      break;
    case GPU_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE:
      /* CUDA's warp size = 32 on all current NVIDIA GPUs. */
      v = 32;
      break;
    default:
      v = 0;
  }
  if (res != CUDA_SUCCESS) return -1;
  if (param_size == sizeof(size_t)) {
    *(size_t *)value = (size_t)v;
  } else if (param_size == sizeof(unsigned int)) {
    *(unsigned int *)value = (unsigned int)v;
  } else {
    return -1;
  }
  return 0;
}

/* Per-thread context lifecycle.  Worker threads call attach before any
 * CUDA call and detach after.  No-op on OpenCL/Metal backends. */
void gpu_thread_attach(void) {
  if (g_default_context == NULL) return;  /* called before any context exists */
  CUresult res = cuCtxPushCurrent(g_default_context);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuCtxPushCurrent failed in worker thread: %s\n", err ? err : "(unknown)");
    exit(-1);
  }
}

void gpu_thread_detach(void) {
  CUcontext old;
  cuCtxPopCurrent(&old);  /* ignore errors; if no context was pushed, this is a no-op */
}
