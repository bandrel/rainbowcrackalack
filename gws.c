#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gpu_backend.h"

#include "gws.h"


/* Given a GPU device and optional kernel name, returns the optimal GWS setting
 * (found through manual experimentation).  Returns 0 if the optimal setting on
 * the device is unknown. */
unsigned int get_optimal_gws(gpu_device device, const char *kernel_name) {
  char vendor[128] = {0}, name[64] = {0};


  get_device_str(device, CL_DEVICE_VENDOR, vendor, sizeof(vendor) - 1);
  get_device_str(device, CL_DEVICE_NAME, name, sizeof(name) - 1);

  if (strcmp(vendor, "NVIDIA Corporation") == 0) {
    unsigned int base_gws = 0;

    if (strcmp(name, "GeForce GTX 1080 Ti") == 0)
      base_gws = 28 * 768; /* Guess based on the 1070 & 1070 Ti's performance. */
    else if (strcmp(name, "GeForce GTX 1080") == 0)
      base_gws = 20 * 768; /* Guess based on the 1070 & 1070 Ti's performance. */
    else if (strcmp(name, "GeForce GTX 1070 Ti") == 0)
      base_gws = 19 * 768; /* NTLM 8-char: ?/s */

    else if (strcmp(name, "GeForce GTX 770") == 0)
      base_gws = 8 * 768; /* ??? */

    else if (strcmp(name, "GeForce GTX 1070") == 0)
      base_gws = 15 * 768; /* NTLM 8-char: 3,028/s */

    else if (strcmp(name, "GeForce GTX 1660 Ti") == 0)
      base_gws = 24 * 1536; /* NTLM 8-char: 8,070/s */

    else if (strcmp(name, "GeForce RTX 2060") == 0)
#ifdef _WIN32
      base_gws = 30 * 256; /* This is a guess based on the RTX 2070's Windows performance.  The RTX 2070's optimal performance is at 36 compute units x 256 = 9216, so maybe the RTX 2060's is 30 compute units x 256 = 7680? */
#else
      base_gws = 30 * 512; /* NTLM 8-char: 5287/s */
#endif

    else if (strcmp(name, "GeForce RTX 2070") == 0)
#ifdef _WIN32
      base_gws = 36 * 256; /* NTLM 8-char: 4,683/s */
#else
      base_gws = 36 * 512; /* NTLM 8-char: 6,345/s */
#endif

    /* The RTX 2080 numbers are an educated guess based on how the RTX 2070 and 2060's numbers.  Their compute units times 512 is optimal for Linux; their compute units times 256 is optimal for Windows. */
    else if (strcmp(name, "GeForce RTX 2080") == 0)
#ifdef _WIN32
      base_gws = 46 * 256;
#else
      base_gws = 46 * 512;
#endif

    /* The RTX 2080 Ti numbers are an educated guess based on how the RTX 2070 and 2060's numbers.  Their compute units times 512 is optimal for Linux; their compute units times 256 is optimal for Windows. */
    else if (strcmp(name, "GeForce RTX 2080 Ti") == 0)
#ifdef _WIN32
      base_gws = 68 * 256;
#else
      base_gws = 68 * 512;
#endif

    else if (strcmp(name, "GeForce RTX 3090") == 0)
#ifdef _WIN32
      base_gws = 82 * 256;
#else
      base_gws = 82 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4090") == 0)
#ifdef _WIN32
      base_gws = 128 * 256;
#else
      base_gws = 128 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4080 SUPER") == 0)
#ifdef _WIN32
      base_gws = 80 * 256;
#else
      base_gws = 80 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4080") == 0)
#ifdef _WIN32
      base_gws = 76 * 256;
#else
      base_gws = 76 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4070 Ti SUPER") == 0)
#ifdef _WIN32
      base_gws = 66 * 256;
#else
      base_gws = 66 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4070 Ti") == 0)
#ifdef _WIN32
      base_gws = 60 * 256;
#else
      base_gws = 60 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4070 SUPER") == 0)
#ifdef _WIN32
      base_gws = 56 * 256;
#else
      base_gws = 56 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4070") == 0)
#ifdef _WIN32
      base_gws = 46 * 256;
#else
      base_gws = 46 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4060 Ti") == 0)
#ifdef _WIN32
      base_gws = 34 * 256;
#else
      base_gws = 34 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 4060") == 0)
#ifdef _WIN32
      base_gws = 24 * 256;
#else
      base_gws = 24 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3080 Ti") == 0)
#ifdef _WIN32
      base_gws = 80 * 256;
#else
      base_gws = 80 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3080") == 0)
#ifdef _WIN32
      base_gws = 68 * 256;
#else
      base_gws = 68 * 768;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3070 Ti") == 0)
#ifdef _WIN32
      base_gws = 48 * 256;
#else
      base_gws = 48 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3070") == 0)
#ifdef _WIN32
      base_gws = 46 * 256;
#else
      base_gws = 46 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3060 Ti") == 0)
#ifdef _WIN32
      base_gws = 38 * 256;
#else
      base_gws = 38 * 512;
#endif

    else if (strcmp(name, "NVIDIA GeForce RTX 3060") == 0)
#ifdef _WIN32
      base_gws = 28 * 256;
#else
      base_gws = 28 * 512;
#endif

    else if (strcmp(name, "Tesla V100-SXM2-16GB") == 0) /* Amazon EC2 p3.2xlarge instance */
      base_gws = 80 * 512;

    else {
      gpu_uint compute_units = 0;
      get_device_uint(device, CL_DEVICE_MAX_COMPUTE_UNITS, &compute_units);
      if (compute_units > 0) {
#ifdef _WIN32
        base_gws = compute_units * 256;
#else
        base_gws = compute_units * 512;
#endif
      }
    }

    if (base_gws > 0)
      return base_gws;
  }

  if (strcmp(vendor, "Advanced Micro Devices, Inc.") == 0) {
    unsigned int base_gws = 0;
    if (strcmp(name, "gfx900") == 0) /* AMD Vega 64 */
#ifdef _WIN32
      base_gws = 64 * 256; /* NTLM 8-char: 2,560/s */
#else
      base_gws = 64 * 768; /* NTLM 8-char: 5,671/s */
#endif
    if (base_gws > 0)
      return base_gws;
  }

#ifdef USE_METAL
  if (strcmp(vendor, "Apple") == 0) {
    unsigned int base_gws = 0;
    if (strstr(name, "M4 Max") != NULL)
      base_gws = 512 * 512;
    else if (strstr(name, "M4 Pro") != NULL)
      base_gws = 384 * 512;
    else if (strstr(name, "M4") != NULL)
      base_gws = 256 * 512;
    else if (strstr(name, "M3 Max") != NULL)
      base_gws = 256 * 512;
    else if (strstr(name, "M3 Pro") != NULL)
      base_gws = 384 * 384;
    else if (strstr(name, "M3") != NULL)
      base_gws = 256 * 384;
    else if (strstr(name, "M2 Ultra") != NULL)
      base_gws = 512 * 384;
    else if (strstr(name, "M2 Max") != NULL)
      base_gws = 384 * 384;
    else if (strstr(name, "M2 Pro") != NULL)
      base_gws = 256 * 384;
    else if (strstr(name, "M2") != NULL)
      base_gws = 256 * 256;
    else if (strstr(name, "M1 Ultra") != NULL)
      base_gws = 384 * 384;
    else if (strstr(name, "M1 Max") != NULL)
      base_gws = 256 * 384;
    else if (strstr(name, "M1 Pro") != NULL)
      base_gws = 256 * 256;
    else if (strstr(name, "M1") != NULL)
      base_gws = 256 * 256;
    else {
      /* Unknown Apple Silicon - memory-based heuristic */
      gpu_ulong mem_size = 0;
      get_device_ulong(device, CL_DEVICE_GLOBAL_MEM_SIZE, &mem_size);
      if (mem_size > 64ULL * 1024 * 1024 * 1024)
        base_gws = 512 * 512;
      else if (mem_size > 32ULL * 1024 * 1024 * 1024)
        base_gws = 384 * 384;
      else if (mem_size > 16ULL * 1024 * 1024 * 1024)
        base_gws = 256 * 384;
      else
        base_gws = 256 * 256;
    }
    return base_gws;
  }
#endif

  return 0;
}

/* Safety headroom required on top of the requested allocation. */
#define GPU_VRAM_SAFETY_MARGIN ((uint64_t)256 * 1024 * 1024)
/* How long to sleep between free-VRAM polls, in microseconds (2 s). */
#define GPU_VRAM_POLL_USEC (2 * 1000 * 1000)

void gpu_wait_for_free_vram(gpu_device device, uint64_t needed_bytes) {
  uint64_t required = needed_bytes + GPU_VRAM_SAFETY_MARGIN;
  int waited = 0;

  for (;;) {
    uint64_t free_bytes = 0, total_bytes = 0;

    /* If the backend can't report VRAM, don't wait at all. */
    if (gpu_get_free_memory(device, &free_bytes, &total_bytes) != 0)
      return;

    if (free_bytes >= required) {
      if (waited)
        printf("GPU memory available again (%"PRIu64" MB free); resuming.\n",
               free_bytes / (1024 * 1024));
      return;
    }

    if (!waited) {
      printf("Waiting for GPU memory: need %"PRIu64" MB, %"PRIu64" MB free. "
             "(Is another GPU process running?)\n",
             required / (1024 * 1024), free_bytes / (1024 * 1024));
      fflush(stdout);
      waited = 1;
    }

    usleep(GPU_VRAM_POLL_USEC);
  }
}
