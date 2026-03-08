#ifndef _GPU_BACKEND_H
#define _GPU_BACKEND_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NUM_PLATFORMS 32
#define MAX_NUM_DEVICES 32

#ifdef USE_METAL

typedef void *gpu_platform;
typedef void *gpu_device;
typedef void *gpu_context;
typedef void *gpu_queue;
typedef void *gpu_buffer;
typedef void *gpu_program;
typedef void *gpu_kernel;

typedef uint32_t gpu_uint;
typedef uint64_t gpu_ulong;
typedef int32_t  gpu_int;
typedef int      gpu_bool;

#define GPU_RO  0x1
#define GPU_WO  0x2
#define GPU_RW  0x3

#define GPU_TRUE  1
#define GPU_FALSE 0

#define GPU_SUCCESS 0

#define DEFAULT_BUILD_OPTIONS "-I. -IMetal"

#else /* OpenCL backend */

#define CL_TARGET_OPENCL_VERSION 200
#include <CL/cl.h>

typedef cl_platform_id gpu_platform;
typedef cl_device_id   gpu_device;
typedef cl_context     gpu_context;
typedef cl_command_queue gpu_queue;
typedef cl_mem         gpu_buffer;
typedef cl_program     gpu_program;
typedef cl_kernel      gpu_kernel;

typedef cl_uint  gpu_uint;
typedef cl_ulong gpu_ulong;
typedef cl_int   gpu_int;
typedef cl_bool  gpu_bool;

#define GPU_RO  CL_MEM_READ_ONLY
#define GPU_WO  CL_MEM_WRITE_ONLY
#define GPU_RW  CL_MEM_READ_WRITE

#define GPU_TRUE  CL_TRUE
#define GPU_FALSE CL_FALSE

#define GPU_SUCCESS CL_SUCCESS

#define DEFAULT_BUILD_OPTIONS "-I. -ICL"

#endif /* USE_METAL */

/* Keep legacy CL_ flag aliases for compatibility with existing macros. */
#define CL_RO GPU_RO
#define CL_WO GPU_WO
#define CL_RW GPU_RW


/* --- Function declarations --- */

void context_callback(const char *errinfo, const void *private_info, size_t cb, void *user_data);

void get_platforms_and_devices(int disable_platform, gpu_uint platforms_buffer_size, gpu_platform *platforms, gpu_uint *num_platforms, gpu_uint devices_buffer_size, gpu_device *devices, gpu_uint *num_devices, unsigned int verbose);

void get_device_bool(gpu_device device, unsigned int param, gpu_bool *b);
void get_device_str(gpu_device device, unsigned int param, char *buf, int buf_len);
void get_device_uint(gpu_device device, unsigned int param, gpu_uint *u);
void get_device_ulong(gpu_device device, unsigned int param, gpu_ulong *ul);

void load_kernel(gpu_context context, gpu_uint num_devices, const gpu_device *devices, const char *path, const char *kernel_name, gpu_program *program, gpu_kernel *kernel, unsigned int hash_type);

void print_device_info(gpu_device *devices, gpu_uint num_devices);

void gpu_release_device(gpu_device device);


/* --- Device info parameter constants --- */
#ifdef USE_METAL

#define GPU_DEVICE_NAME                 1
#define GPU_DEVICE_VERSION              2
#define GPU_DEVICE_VENDOR               3
#define GPU_DEVICE_AVAILABLE            4
#define GPU_DEVICE_MAX_COMPUTE_UNITS    5
#define GPU_DEVICE_GLOBAL_MEM_SIZE      6
#define GPU_DEVICE_MAX_WORK_GROUP_SIZE  7
#define GPU_DRIVER_VERSION              8

#define GPU_KERNEL_WORK_GROUP_SIZE                       1
#define GPU_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE    2

/* Use GPU_* names in consumer code via macros. */
#define CL_DEVICE_NAME                GPU_DEVICE_NAME
#define CL_DEVICE_VERSION             GPU_DEVICE_VERSION
#define CL_DEVICE_VENDOR              GPU_DEVICE_VENDOR
#define CL_DEVICE_AVAILABLE           GPU_DEVICE_AVAILABLE
#define CL_DEVICE_MAX_COMPUTE_UNITS   GPU_DEVICE_MAX_COMPUTE_UNITS
#define CL_DEVICE_GLOBAL_MEM_SIZE     GPU_DEVICE_GLOBAL_MEM_SIZE
#define CL_DEVICE_MAX_WORK_GROUP_SIZE GPU_DEVICE_MAX_WORK_GROUP_SIZE
#define CL_DRIVER_VERSION             GPU_DRIVER_VERSION

#define CL_KERNEL_WORK_GROUP_SIZE                    GPU_KERNEL_WORK_GROUP_SIZE
#define CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE GPU_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE

#else /* OpenCL - these are already defined by CL/cl.h */

#define GPU_DEVICE_NAME                CL_DEVICE_NAME
#define GPU_DEVICE_VERSION             CL_DEVICE_VERSION
#define GPU_DEVICE_VENDOR              CL_DEVICE_VENDOR
#define GPU_DEVICE_AVAILABLE           CL_DEVICE_AVAILABLE
#define GPU_DEVICE_MAX_COMPUTE_UNITS   CL_DEVICE_MAX_COMPUTE_UNITS
#define GPU_DEVICE_GLOBAL_MEM_SIZE     CL_DEVICE_GLOBAL_MEM_SIZE
#define GPU_DEVICE_MAX_WORK_GROUP_SIZE CL_DEVICE_MAX_WORK_GROUP_SIZE
#define GPU_DRIVER_VERSION             CL_DRIVER_VERSION

#define GPU_KERNEL_WORK_GROUP_SIZE                    CL_KERNEL_WORK_GROUP_SIZE
#define GPU_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE

#endif


/* --- Low-level backend operations --- */

#ifdef USE_METAL

gpu_context gpu_create_context(gpu_device device);
gpu_queue   gpu_create_queue(gpu_context context, gpu_device device);
gpu_buffer  gpu_create_buffer(gpu_context context, int flags, size_t size);
int gpu_write_buffer(gpu_queue queue, gpu_buffer buffer, size_t size, const void *ptr);
int gpu_read_buffer(gpu_queue queue, gpu_buffer buffer, size_t size, void *ptr);
int gpu_set_kernel_arg(gpu_kernel kernel, unsigned int index, size_t size, const void *value);
int gpu_enqueue_kernel(gpu_queue queue, gpu_kernel kernel, unsigned int work_dim, const size_t *global_work_size);
int gpu_flush(gpu_queue queue);
int gpu_finish(gpu_queue queue);
int gpu_get_kernel_work_group_info(gpu_kernel kernel, gpu_device device, unsigned int param, size_t param_size, void *param_value);
void gpu_release_buffer(gpu_buffer buffer);
void gpu_release_queue(gpu_queue queue);
void gpu_release_context(gpu_context context);
void gpu_release_kernel(gpu_kernel kernel);
void gpu_release_program(gpu_program program);

#else /* OpenCL backend - declare extern function pointers from opencl_setup.c */

void *rc_dlopen(char *library_name);
int rc_dlclose(void *module);
void *rc_dlsym(void *module, char *function_name);
char *rc_dlerror(void);

extern cl_int (*rc_clBuildProgram)(cl_program, cl_uint, const cl_device_id *, const char *, void (CL_CALLBACK *)(cl_program, void *), void *);
extern cl_mem (*rc_clCreateBuffer)(cl_context, cl_mem_flags, size_t, void *, cl_int *);
extern cl_context (*rc_clCreateContext)(cl_context_properties *, cl_uint, const cl_device_id *, void (CL_CALLBACK *)(const char *, const void *, size_t, void *), void *, cl_int *);
extern cl_command_queue (*rc_clCreateCommandQueueWithProperties)(cl_context, cl_device_id, const cl_queue_properties *, cl_int *);
extern cl_kernel (*rc_clCreateKernel)(cl_program, const char *, cl_int *);
extern cl_program (*rc_clCreateProgramWithSource)(cl_context, cl_uint, const char **, const size_t *, cl_int *);
extern cl_int (*rc_clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t *, const size_t *, const size_t *, cl_uint, const cl_event *, cl_event *);
extern cl_int (*rc_clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
extern cl_int (*rc_clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void *, cl_uint, const cl_event *, cl_event *);
extern cl_int (*rc_clFinish)(cl_command_queue);
extern cl_int (*rc_clFlush)(cl_command_queue);
extern cl_int (*rc_clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
extern cl_int (*rc_clGetDeviceInfo)(cl_device_id, cl_device_info, size_t, void *, size_t *);
extern cl_int (*rc_clGetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_kernel_work_group_info, size_t, void *, size_t *);
extern cl_int (*rc_clGetPlatformIDs)(cl_uint, cl_platform_id *, cl_uint *);
extern cl_int (*rc_clGetPlatformInfo)(cl_platform_id, cl_platform_info, size_t, void *, size_t *);
extern cl_int (*rc_clGetProgramBuildInfo)(cl_program, cl_device_id, cl_program_build_info, size_t, void *, size_t *);
extern cl_int (*rc_clReleaseCommandQueue)(cl_command_queue);
extern cl_int (*rc_clReleaseContext)(cl_context);
extern cl_int (*rc_clReleaseDevice)(cl_device_id);
extern cl_int (*rc_clReleaseKernel)(cl_kernel);
extern cl_int (*rc_clReleaseMemObject)(cl_mem);
extern cl_int (*rc_clReleaseProgram)(cl_program);
extern cl_int (*rc_clSetKernelArg)(cl_kernel, cl_uint, size_t, const void *);

#endif /* USE_METAL */


/* --- Convenience macros (backward compatible) --- */

#define CLMAKETESTVARS() \
  int err = 0; \
  size_t global_work_size = 1; \
  gpu_queue queue = NULL;

#ifdef USE_METAL

#define _CLCREATEARG(_arg_index, _buffer, _flags, _arg_ptr, _arg_size) \
  { _buffer = gpu_create_buffer(context, _flags, _arg_size); \
  if (_buffer == NULL) { fprintf(stderr, "Error while creating buffer for \"%s\".\n", #_arg_ptr); exit(-1); } \
  if (gpu_write_buffer(queue, _buffer, _arg_size, _arg_ptr) != 0) { fprintf(stderr, "Error while writing to buffer for \"%s\".\n", #_arg_ptr); exit(-1); } \
  if (gpu_set_kernel_arg(kernel, _arg_index, sizeof(gpu_buffer), &_buffer) != 0) { fprintf(stderr, "Error setting kernel argument for %s at index %u.\n", #_arg_ptr, _arg_index); exit(-1); } }

#define CLCREATEARG_ARRAY(_arg_index, _buffer, _flags, _arg, _len) \
  _CLCREATEARG(_arg_index, _buffer, _flags, _arg, _len);

#define CLCREATEARG(_arg_index, _buffer, _flags, _arg, _arg_size) \
  _CLCREATEARG(_arg_index, _buffer, _flags, &_arg, _arg_size);

#define CLCREATEARG_DEBUG(_arg_index, _debug_buffer, _debug_ptr) \
  { _debug_ptr = calloc(DEBUG_LEN, sizeof(unsigned char)); \
    CLCREATEARG_ARRAY(_arg_index, _debug_buffer, GPU_RW, _debug_ptr, DEBUG_LEN); }

#define CLCREATECONTEXT(_context_callback, _device_ptr) \
  gpu_create_context(*(_device_ptr))

#define CLCREATEQUEUE(_context, _device) \
  gpu_create_queue(_context, _device)

#define CLRUNKERNEL(_queue, _kernel, _gws_ptr) \
  { err = gpu_enqueue_kernel(_queue, _kernel, 1, _gws_ptr); if (err != 0) { fprintf(stderr, "gpu_enqueue_kernel failed: %d\n", err); exit(-1); } }

#define CLFLUSH(_queue) \
  { err = gpu_flush(_queue); if (err != 0) { fprintf(stderr, "gpu_flush failed: %d\n", err); exit(-1); } }

#define CLWAIT(_queue) \
  { err = gpu_finish(_queue); if (err != 0) { fprintf(stderr, "gpu_finish failed: %d\n", err); exit(-1); } }

#define CLWRITEBUFFER(_buffer, _len, _ptr) \
  { err = gpu_write_buffer(queue, _buffer, _len, _ptr); \
  if (err != 0) { fprintf(stderr, "gpu_write_buffer failed: %d\n", err); exit(-1); } }

#define CLREADBUFFER(_buffer, _len, _ptr) \
  { err = gpu_read_buffer(queue, _buffer, _len, _ptr); if (err != 0) { fprintf(stderr, "gpu_read_buffer failed: %d\n", err); exit(-1); } }

#define CLFREEBUFFER(_buffer) \
  if (_buffer != NULL) { gpu_release_buffer(_buffer); _buffer = NULL; }

#define CLRELEASEQUEUE(_queue) \
  if (_queue != NULL) { gpu_release_queue(_queue); _queue = NULL; }

#define CLRELEASECONTEXT(_context) \
  if (_context != NULL) { gpu_release_context(_context); _context = NULL; }

#define CLRELEASEKERNEL(_kernel) \
  if (_kernel != NULL) { gpu_release_kernel(_kernel); _kernel = NULL; }

#define CLRELEASEPROGRAM(_program) \
  if (_program != NULL) { gpu_release_program(_program); _program = NULL; }

#else /* OpenCL backend - original macros */

#define _CLCREATEARG(_arg_index, _buffer, _flags, _arg_ptr, _arg_size) \
  { _buffer = rc_clCreateBuffer(context, _flags, _arg_size, NULL, &err); \
  if (err < 0) { fprintf(stderr, "Error while creating buffer for \"%s\". Error code: %d\n", #_arg_ptr, err); exit(-1); } \
  err = rc_clEnqueueWriteBuffer(queue, _buffer, CL_TRUE, 0, _arg_size, _arg_ptr, 0, NULL, NULL); \
  if (err < 0) { fprintf(stderr, "Error while writing to buffer for \"%s\". Error code: %d\n", #_arg_ptr, err); exit(-1); } \
  err = rc_clSetKernelArg(kernel, _arg_index, sizeof(cl_mem), &_buffer); \
  if (err < 0) { fprintf(stderr, "Error setting kernel argument for %s at index %u.\n", #_arg_ptr, _arg_index); exit(-1); } }

#define CLCREATEARG_ARRAY(_arg_index, _buffer, _flags, _arg, _len) \
  _CLCREATEARG(_arg_index, _buffer, _flags, _arg, _len);

#define CLCREATEARG(_arg_index, _buffer, _flags, _arg, _arg_size) \
  _CLCREATEARG(_arg_index, _buffer, _flags, &_arg, _arg_size);

#define CLCREATEARG_DEBUG(_arg_index, _debug_buffer, _debug_ptr) \
  { _debug_ptr = calloc(DEBUG_LEN, sizeof(unsigned char)); \
    CLCREATEARG_ARRAY(_arg_index, _debug_buffer, CL_MEM_READ_WRITE, _debug_ptr, DEBUG_LEN); }

#define CLCREATECONTEXT(_context_callback, _device_ptr) \
  rc_clCreateContext(NULL, 1, _device_ptr, _context_callback, NULL, &err); if (err < 0) { fprintf(stderr, "Failed to create context: %d\n", err); exit(-1); }

#define CLCREATEQUEUE(_context, _device) \
  rc_clCreateCommandQueueWithProperties(_context, _device, NULL, &err); if (err < 0) { fprintf(stderr, "clCreateCommandQueueWithProperties failed: %d\n", err); exit(-1); }

#define CLRUNKERNEL(_queue, _kernel, _gws_ptr) \
  { err = rc_clEnqueueNDRangeKernel(_queue, _kernel, 1, NULL, _gws_ptr, NULL, 0, NULL, NULL); if (err < 0) { fprintf(stderr, "clEnqueueNDRangeKernel failed: %d\n", err);  exit(-1); } }

#define CLFLUSH(_queue) \
  { err = rc_clFlush(_queue); if (err < 0) { fprintf(stderr, "clFlush failed: %d\n", err); exit(-1); } }

#define CLWAIT(_queue) \
  { err = rc_clFinish(_queue); if (err == CL_INVALID_COMMAND_QUEUE) { fprintf(stderr, "\nError: clFinish() returned CL_INVALID_COMMAND_QUEUE (%d).  This is often caused by running out of host memory.  Sometimes, it can be worked around by lowering the GWS setting (see command line options; hint: try setting it to a multiple of the max compute units reported at the beginning of the program output.  For example, if the MCU is 15, try setting the GWS parameter to 15 * 256 = 3840, 15 * 1024 = 15360, etc).\n", err); exit(-1); } else if (err < 0) { fprintf(stderr, "clFinish failed: %d\n", err); exit(-1); } }

#define CLWRITEBUFFER(_buffer, _len, _ptr) \
  { err = rc_clEnqueueWriteBuffer(queue, _buffer, CL_TRUE, 0, _len, _ptr, 0, NULL, NULL); \
  if (err < 0) { fprintf(stderr, "clEnqueueWriteBuffer failed: %d\n", err); exit(-1); } }

#define CLREADBUFFER(_buffer, _len, _ptr) \
  { err = rc_clEnqueueReadBuffer(queue, _buffer, CL_TRUE, 0, _len, _ptr, 0, NULL, NULL); if (err < 0) { fprintf(stderr, "clEnqueueReadBuffer failed: %d\n", err); exit(-1); } }

#define CLFREEBUFFER(_buffer) \
  if (_buffer != NULL) { rc_clReleaseMemObject(_buffer); _buffer = NULL; }

#define CLRELEASEQUEUE(_queue) \
  if (_queue != NULL) { rc_clReleaseCommandQueue(_queue); _queue = NULL; }

#define CLRELEASECONTEXT(_context) \
  if (_context != NULL) { rc_clReleaseContext(_context); _context = NULL; }

#define CLRELEASEKERNEL(_kernel) \
  if (_kernel != NULL) { rc_clReleaseKernel(_kernel); _kernel = NULL; }

#define CLRELEASEPROGRAM(_program) \
  if (_program != NULL) { rc_clReleaseProgram(_program); _program = NULL; }

#define LOADFUNC(_ocl, _func_name) \
  { rc_##_func_name = rc_dlsym(_ocl, #_func_name); \
    if (rc_##_func_name == NULL) { fprintf(stderr, "Error while loading function %s: %s\n", #_func_name, rc_dlerror()); exit(-1); } }

#endif /* USE_METAL */

#endif /* _GPU_BACKEND_H */
