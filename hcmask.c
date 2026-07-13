#include "hcmask.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Split `line` into up to 5 fields on UNESCAPED commas, unescaping "\," -> ','
 * and "\#" -> '#' (anywhere in a field; only strictly needed at line start to
 * avoid comment detection, but unescaping it elsewhere is harmless since '#'
 * is otherwise literal).  All other backslash sequences pass through verbatim
 * (so \xNN / \\ reach expand_charset_def later).
 * Returns 1 = entry produced, 0 = skipped, -1 = error. */
int hcmask_parse_line(const char *line, HcmaskEntry *out) {
    char buf[HCMASK_MAX_MASK_LEN + 4 * (MAX_CHARSET_LEN + 1) + 16];
    size_t li = 0, bi = 0;
    char *fields[6];
    int nfields = 0;
    int i;

    if (line == NULL || out == NULL)
        return -1;

    /* Skip a leading whitespace-only / blank line, or a comment. */
    {
        const char *p = line;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '\0') return 0;
        if (*p == '#') return 0;   /* comment (first non-space char is '#') */
    }

    memset(out, 0, sizeof(*out));

    /* Copy into buf, unescaping \, and \# , splitting on unescaped ',' by
     * writing NULs and recording field starts.  Strip a trailing newline. */
    fields[nfields++] = buf + bi;
    while (line[li] != '\0' && line[li] != '\n' && line[li] != '\r') {
        if (bi + 1 >= sizeof(buf)) return -1;   /* line too long */
        if (line[li] == '\\' && line[li + 1] == ',') {
            buf[bi++] = ','; li += 2;
        } else if (line[li] == '\\' && line[li + 1] == '#') {
            buf[bi++] = '#'; li += 2;
        } else if (line[li] == ',') {
            buf[bi++] = '\0';
            if (nfields >= 6) return -1;          /* > 5 fields */
            fields[nfields++] = buf + bi;
            li++;
        } else {
            buf[bi++] = line[li++];
        }
    }
    buf[bi] = '\0';

    /* Catches the exactly-6-fields case (5 commas, e.g. "a,b,c,d,e,?1?1").
     * The in-loop guard above only fires on a 7th field (6th comma). */
    if (nfields > 5) return -1;

    /* Last field is the mask; must be non-empty. */
    {
        const char *maskf = fields[nfields - 1];
        if (maskf[0] == '\0') return -1;          /* empty mask */
        if (strlen(maskf) >= HCMASK_MAX_MASK_LEN) return -1;
        strcpy(out->mask, maskf);
    }

    /* Preceding fields are custom charsets ?1..?4 in order. */
    for (i = 0; i < nfields - 1; i++) {
        if (strlen(fields[i]) > MAX_CHARSET_LEN) return -1;
        strcpy(out->cc[i], fields[i]);
        out->has_cc[i] = 1;
    }

    return 1;
}

int hcmask_load(const char *path, HcmaskEntry **out, int *count) {
    FILE *f;
    char line[4096];
    HcmaskEntry *entries = NULL;
    int cap = 0, n = 0, lineno = 0;

    if (!path || !out || !count) return -1;
    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "hcmask: cannot open %s\n", path); return -1; }

    while (fgets(line, sizeof(line), f) != NULL) {
        HcmaskEntry e;
        int r;
        lineno++;
        r = hcmask_parse_line(line, &e);
        if (r == 0) continue;                 /* comment/blank */
        if (r < 0) {
            fprintf(stderr, "%s:%d: invalid .hcmask line\n", path, lineno);
            free(entries); fclose(f); return -1;
        }
        if (n >= cap) {
            int ncap = cap ? cap * 2 : 16;
            HcmaskEntry *ne = realloc(entries, (size_t)ncap * sizeof(HcmaskEntry));
            if (!ne) { free(entries); fclose(f); return -1; }
            entries = ne; cap = ncap;
        }
        entries[n++] = e;
    }
    fclose(f);

    if (n == 0) {
        fprintf(stderr, "%s: no masks found\n", path);
        free(entries); return -1;
    }
    *out = entries; *count = n;
    return 0;
}
