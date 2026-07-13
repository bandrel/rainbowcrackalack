#ifndef HCMASK_H
#define HCMASK_H
#include "shared.h"      /* MAX_CHARSET_LEN */

#define HCMASK_MAX_MASK_LEN 128   /* longest legal mask string */

typedef struct {
    char mask[HCMASK_MAX_MASK_LEN];      /* the mask field, e.g. "?1?1?l?d" */
    char cc[4][MAX_CHARSET_LEN + 1];     /* inline custom-charset defs (raw, NUL-terminated) */
    int  has_cc[4];                      /* 1 if slot N was defined inline on this line */
} HcmaskEntry;

/* Parse ONE .hcmask line into *out.
 * Returns 1 = entry produced, 0 = skipped (comment/blank), -1 = parse error. */
int hcmask_parse_line(const char *line, HcmaskEntry *out);

/* Load a whole .hcmask file into a malloc'd array (*out, caller frees).
 * On a bad line prints "<path>:<lineno>: <reason>" to stderr and returns -1.
 * Returns 0 on success (sets *out, *count), -1 on error. */
int hcmask_load(const char *path, HcmaskEntry **out, int *count);

#endif
