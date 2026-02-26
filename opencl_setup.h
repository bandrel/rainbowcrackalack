#ifndef _OPENCL_SETUP_H
#define _OPENCL_SETUP_H

#include "gpu_backend.h"

/* Enable USE_DES_BITSLICE to use the DES bitslice code from JohnTheRipper.  At this time, it somehow runs at half the speed of unoptimized DES on NVIDIA.  Anyone else want to look into what's going on? */
/*#define USE_DES_BITSLICE 1*/

#ifndef USE_METAL
void get_platform_str(cl_platform_id platform, cl_platform_info param, char *buf, size_t buf_len);
void print_platform_info(cl_platform_id *platforms, cl_uint num_platforms);
#endif

#endif
