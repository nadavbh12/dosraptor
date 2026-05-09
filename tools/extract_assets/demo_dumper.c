/*
 * demo_dumper.c - Extract Raptor demo recordings (_REC items) to JSON.
 *
 * Demo format (from SOURCE/INPUT.C and SOURCE/PUBLIC.H):
 *
 *   The GLB item is an array of RECORD structs.
 *   Record[0] is the header:
 *     playerpic = max_play  (total number of records including header)
 *     px        = demo_game (which game: 0, 1, or 2)
 *     py        = demo_wave (which wave/level)
 *
 *   Records[1 .. max_play-1] are per-tick input snapshots.
 *
 * RECORD (from SOURCE/PUBLIC.H, Watcom layout, 12 bytes total):
 *   BYTE  b1, b2, b3, b4;  -- control bytes (fire, movement flags, etc.)
 *   SHORT px, py;           -- player X and Y position
 *   SHORT playerpic;        -- current player sprite index
 *   SHORT fil;              -- padding / alignment
 */

/* Include standard headers first to avoid macro conflicts from types.h
 * (which defines `random` as a macro that breaks <stdlib.h>). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stddef.h>

/* Legacy GLB API — must come after standard headers. */
#include "demo_dumper.h"
#include "GLBAPI.H"

/* DemoRecord matches RECORD in SOURCE/PUBLIC.H.
 * Packed to guarantee 12-byte layout regardless of host ABI. */
typedef struct {
    unsigned char b1, b2, b3, b4;
    short px;
    short py;
    short playerpic;
    short fil;
} __attribute__((packed)) DemoRecord;

_Static_assert(sizeof(DemoRecord) == 12, "DemoRecord must be 12 bytes");

/* case-insensitive suffix check */
static int has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen) return 0;
    return (strncasecmp(name + nlen - slen, suffix, slen) == 0);
}

int dump_demo_items(const char *outdir)
{
    /* Create <outdir>/demos directory. */
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/demos", outdir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        perror(dir);
        return 0;
    }

    /* Total items across all loaded GLB archives. */
    int total = 0;
    for (int f = 0; f < 15; f++) {
        int n = GLB_GetFileItems(f);
        if (n == 0) break;
        total += n;
    }

    int written = 0;

    for (int i = 0; i < total; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(i, name, sizeof name, &handle, &sz) != 0) continue;

        /* Only process _REC items (demo recordings). */
        if (!has_suffix(name, "_REC")) continue;

        /* Must be at least one record (the header). */
        if (sz < sizeof(DemoRecord)) {
            fprintf(stderr, "[demo skip tiny] %s size=%zu\n", name, sz);
            continue;
        }

        BYTE *mem = GLB_GetItem(handle);
        if (!mem) {
            fprintf(stderr, "[demo skip null] %s\n", name);
            continue;
        }

        size_t num_records = sz / sizeof(DemoRecord);
        DemoRecord *recs = (DemoRecord *)mem;

        /* Header is record[0]. */
        int max_play  = (int)(unsigned short)recs[0].playerpic;
        int demo_game = (int)(short)recs[0].px;
        int demo_wave = (int)(short)recs[0].py;

        /* Clamp max_play to available data to avoid out-of-bounds. */
        if (max_play <= 0 || (size_t)max_play > num_records)
            max_play = (int)num_records;

        /* Build output path: <outdir>/demos/<name>.json */
        char path[4096 + 80];
        snprintf(path, sizeof path, "%s/%s.json", dir, name);

        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "[demo open error] %s: %s\n", path, strerror(errno));
            GLB_FreeItem(handle);
            continue;
        }

        fprintf(fp, "{\n");
        fprintf(fp, "  \"header\": { \"max_play\": %d, \"demo_game\": %d,"
                    " \"demo_wave\": %d },\n",
                max_play, demo_game, demo_wave);
        fprintf(fp, "  \"records\": [\n");

        /* Emit records[1 .. max_play-1] (the actual per-tick data). */
        for (int r = 1; r < max_play && r < (int)num_records; r++) {
            DemoRecord *rec = &recs[r];
            int is_last = (r == max_play - 1) ||
                          (r == (int)num_records - 1);
            fprintf(fp,
                    "    {\"frame\": %d, \"b1\": %u, \"b2\": %u,"
                    " \"b3\": %u, \"b4\": %u,"
                    " \"px\": %d, \"py\": %d, \"playerpic\": %d}%s\n",
                    r - 1,
                    (unsigned)rec->b1, (unsigned)rec->b2,
                    (unsigned)rec->b3, (unsigned)rec->b4,
                    (int)rec->px, (int)rec->py,
                    (int)rec->playerpic,
                    is_last ? "" : ",");
        }

        fprintf(fp, "  ]\n");
        fprintf(fp, "}\n");
        fclose(fp);

        GLB_FreeItem(handle);
        written++;
        fprintf(stdout, "demo: wrote %s (%d records)\n", path, max_play - 1);
    }

    fprintf(stdout, "extract_assets: dumped %d demo files\n", written);
    return written;
}
