/*
 * level_dumper.c - Extract Raptor MAZELEVEL items (_MAP suffix) to JSON.
 *
 * Level format (from SOURCE/MAP.H and SOURCE/LOADSAVE.C):
 *
 *   The GLB item is laid out as:
 *     MAZELEVEL header (5412 bytes):
 *       DWORD sizerec;         -- 4 bytes
 *       DWORD spriteoff;       -- 4 bytes (offset from item start to CSPRITEs)
 *       INT   numsprites;      -- 4 bytes
 *       MAZEDATA map[1350];    -- 1350 * 4 = 5400 bytes
 *         (each MAZEDATA: SHORT flats + SHORT fgame = 4 bytes)
 *     CSPRITE records[numsprites] immediately after the MAZELEVEL header:
 *       INT link, slib, x, y, game; -- 5 * 4 = 20 bytes
 *       DWORD level;                -- 4 bytes
 *       Total per CSPRITE: 24 bytes
 *
 * Item names follow the pattern MAPnGm_MAP (e.g. MAP1G1_MAP, MAP3G2_MAP).
 * Items from the shareware FILE0001.GLB have names ending in _MAP.
 *
 * Type sizes: compiled for DOS/386 (32-bit protected mode), so INT = 4 bytes,
 * SHORT = 2 bytes, DWORD = 4 bytes — matching the macOS 64-bit host.
 *
 * Output: <outdir>/levels/<name>.json, one file per level.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#include "level_dumper.h"
#include "GLBAPI.H"

#define MAP_ROWS  150
#define MAP_COLS  9
#define MAP_SIZE  (MAP_ROWS * MAP_COLS)

/* On-disk layout structs using fixed-width types to ensure binary
 * compatibility with the 32-bit DOS Raptor game data. */
typedef struct {
    int16_t flats;
    int16_t fgame;
} __attribute__((packed)) MazeData;

typedef struct {
    uint32_t sizerec;
    uint32_t spriteoff;
    int32_t  numsprites;
    MazeData map[MAP_SIZE];
} __attribute__((packed)) MazeLevel;

_Static_assert(sizeof(MazeData)  == 4,    "MazeData must be 4 bytes");
_Static_assert(sizeof(MazeLevel) == 5412, "MazeLevel must be 5412 bytes");

typedef struct {
    int32_t  link;
    int32_t  slib;
    int32_t  x;
    int32_t  y;
    int32_t  game;
    uint32_t level;
} __attribute__((packed)) CSprite;

_Static_assert(sizeof(CSprite) == 24, "CSprite must be 24 bytes");

/* case-insensitive suffix check */
static int has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen) return 0;
    return (strncasecmp(name + nlen - slen, suffix, slen) == 0);
}

int dump_level_items(const char *outdir)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/levels", outdir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        perror(dir);
        return 0;
    }

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

        if (!has_suffix(name, "_MAP")) continue;

        if (sz < sizeof(MazeLevel)) {
            fprintf(stderr, "[level skip tiny] %s size=%zu (expected>=%zu)\n",
                    name, sz, sizeof(MazeLevel));
            continue;
        }

        BYTE *mem = GLB_GetItem(handle);
        if (!mem) {
            fprintf(stderr, "[level skip null] %s\n", name);
            continue;
        }

        MazeLevel *ml = (MazeLevel *)mem;

        /* Validate numsprites against item size. */
        int numsprites = (int)ml->numsprites;
        if (numsprites < 0) numsprites = 0;
        size_t expected = sizeof(MazeLevel) + (size_t)numsprites * sizeof(CSprite);
        if (sz < expected) {
            /* Clamp numsprites to what fits in the item. */
            numsprites = (int)((sz - sizeof(MazeLevel)) / sizeof(CSprite));
        }

        CSprite *sprites = (CSprite *)(mem + sizeof(MazeLevel));

        char path[4096 + 80];
        snprintf(path, sizeof path, "%s/%s.json", dir, name);

        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "[level open error] %s: %s\n", path, strerror(errno));
            GLB_FreeItem(handle);
            continue;
        }

        fprintf(fp, "{\n");
        fprintf(fp, "  \"name\": \"%s\",\n", name);
        fprintf(fp, "  \"size\": %d,\n", MAP_SIZE);
        fprintf(fp, "  \"rows\": %d,\n", MAP_ROWS);
        fprintf(fp, "  \"cols\": %d,\n", MAP_COLS);
        fprintf(fp, "  \"sizerec\": %u,\n", (unsigned)ml->sizerec);
        fprintf(fp, "  \"spriteoff\": %u,\n", (unsigned)ml->spriteoff);
        fprintf(fp, "  \"numsprites\": %d,\n", numsprites);

        /* Tile array. */
        fprintf(fp, "  \"tiles\": [\n");
        for (int t = 0; t < MAP_SIZE; t++) {
            int is_last = (t == MAP_SIZE - 1);
            fprintf(fp, "    {\"flats\": %d, \"fgame\": %d}%s\n",
                    (int)ml->map[t].flats,
                    (int)ml->map[t].fgame,
                    is_last ? "" : ",");
        }
        fprintf(fp, "  ],\n");

        /* Sprite array. */
        fprintf(fp, "  \"sprites\": [\n");
        for (int s = 0; s < numsprites; s++) {
            int is_last = (s == numsprites - 1);
            fprintf(fp,
                    "    {\"link\": %d, \"slib\": %d, \"x\": %d, \"y\": %d,"
                    " \"game\": %d, \"level\": %u}%s\n",
                    (int)sprites[s].link,
                    (int)sprites[s].slib,
                    (int)sprites[s].x,
                    (int)sprites[s].y,
                    (int)sprites[s].game,
                    (unsigned)sprites[s].level,
                    is_last ? "" : ",");
        }
        fprintf(fp, "  ]\n");
        fprintf(fp, "}\n");

        fclose(fp);
        GLB_FreeItem(handle);
        written++;
        fprintf(stdout, "level: wrote %s (numsprites=%d, sz=%zu)\n",
                path, numsprites, sz);
    }

    fprintf(stdout, "extract_assets: dumped %d level files\n", written);
    return written;
}
