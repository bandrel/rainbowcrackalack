/* RAR-wrapped rainbow table support depends on libunrar, which is only wired up
 * on the Linux OpenCL/CUDA builds (-DHAVE_UNRAR).  On macOS/Windows this file
 * compiles to an empty object so the shared source wildcard keeps working. */
#ifdef HAVE_UNRAR

#define _UNIX
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unrar/dll.hpp>

#include "rar_decompress.h"

#define RAR_INITIAL_CAPACITY (64 * 1024 * 1024)

typedef struct {
  unsigned char *buf;
  size_t size;
  size_t capacity;
} membuf;


static int CALLBACK rar_data_callback(UINT msg, LPARAM user_data, LPARAM p1, LPARAM p2) {
  if (msg == UCM_PROCESSDATA) {
    membuf *mb = (membuf *)user_data;
    size_t needed = mb->size + p2;

    if (needed > mb->capacity) {
      size_t new_cap = mb->capacity * 2;
      if (new_cap < needed)
        new_cap = needed;
      unsigned char *tmp = (unsigned char *)realloc(mb->buf, new_cap);
      if (!tmp)
        return -1;
      mb->buf = tmp;
      mb->capacity = new_cap;
    }

    memcpy(mb->buf + mb->size, (void *)p1, p2);
    mb->size += p2;
    return 1;
  }
  return 0;
}


/* Decompresses a RAR archive containing a single rainbow table file into memory.
 * Returns 0 on success, or a negative error code. */
int rar_decompress(char *filename, uint64_t **ret_uncompressed_table, unsigned int *ret_num_chains) {
  struct RAROpenArchiveDataEx arc_data;
  struct RARHeaderDataEx hdr;
  HANDLE h = NULL;
  membuf mb = {0, 0, 0};
  int ret = 0, r = 0;

  *ret_uncompressed_table = NULL;
  *ret_num_chains = 0;

  memset(&arc_data, 0, sizeof(arc_data));
  arc_data.ArcName = filename;
  arc_data.OpenMode = RAR_OM_EXTRACT;

  h = RAROpenArchiveEx(&arc_data);
  if (!h || arc_data.OpenResult != ERAR_SUCCESS) {
    fprintf(stderr, "Error: failed to open RAR archive %s: error %u\n", filename, arc_data.OpenResult);
    ret = -1;
    goto done;
  }

  mb.capacity = RAR_INITIAL_CAPACITY;
  mb.buf = (unsigned char *)malloc(mb.capacity);
  if (!mb.buf) {
    fprintf(stderr, "Error: could not allocate initial buffer for RAR decompression.\n");
    ret = -2;
    goto done;
  }

  RARSetCallback(h, rar_data_callback, (LPARAM)&mb);

  memset(&hdr, 0, sizeof(hdr));
  r = RARReadHeaderEx(h, &hdr);
  if (r != ERAR_SUCCESS) {
    fprintf(stderr, "Error: failed to read RAR header from %s: error %d\n", filename, r);
    ret = -3;
    goto done;
  }

  r = RARProcessFile(h, RAR_TEST, NULL, NULL);
  if (r != ERAR_SUCCESS) {
    fprintf(stderr, "Error: failed to decompress RAR file %s: error %d\n", filename, r);
    ret = -4;
    goto done;
  }

  if (mb.size == 0 || mb.size % (sizeof(uint64_t) * 2) != 0) {
    fprintf(stderr, "Error: decompressed RAR data size (%zu) is not a valid rainbow table (must be a multiple of %zu).\n", mb.size, sizeof(uint64_t) * 2);
    ret = -5;
    goto done;
  }

  *ret_uncompressed_table = (uint64_t *)mb.buf;
  *ret_num_chains = mb.size / (sizeof(uint64_t) * 2);
  mb.buf = NULL;

done:
  if (h)
    RARCloseArchive(h);

  if (mb.buf)
    free(mb.buf);

  return ret;
}

#endif /* HAVE_UNRAR */
