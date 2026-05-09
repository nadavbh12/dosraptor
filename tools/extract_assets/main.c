// tools/extract_assets/main.c
//
// One-shot extractor: reads two FILE000?.GLB archives, emits sprites,
// levels, demos, sounds, music into <output_dir>/.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include "GLBAPI.H"
#include "GFXAPI.H"
#include "png_writer.h"
#include "demo_dumper.h"
#include "level_dumper.h"
#include "sprite_meta_dumper.h"

/*
 * GLB_GetFileItems() is declared in glbapi.h after our addition.
 * GLB_InitSystem() signature (verified from GFX/GLBAPI.C):
 *
 *   INT GLB_InitSystem(CHAR *exepath, INT innum, CHAR *iprefix);
 *
 *   exepath   - path used to derive a search directory (strips after last '\')
 *   innum     - number of GLB files to open (e.g. 2 for FILE0000 + FILE0001)
 *   iprefix   - filename prefix, or NULL to use default "FILE"
 *
 * GLB_GetFileItems() added by us in GFX/GLBAPI.C:
 *
 *   INT GLB_GetFileItems(INT filenum);
 *
 *   Returns filedesc[filenum].items after GLB_InitSystem has run.
 *   GLB_NumItems() is PRIVATE (static) so it cannot be called from here.
 *
 * There is no GLB_End() / teardown function in the API.
 */

/*
 * name_has_suffix() - case-insensitive check for a name suffix.
 * Returns 1 if name ends with suffix (case-insensitive), 0 otherwise.
 */
static int name_has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen) return 0;
    return (strncasecmp(name + nlen - slen, suffix, slen) == 0);
}

/*
 * make_dir() - create a directory, returning 0 on success or if it exists.
 */
static int make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        perror(path);
        return -1;
    }
    return 0;
}

/*
 * extract_sprites() - dump all _PIC items that are linear (GPIC) as PNG.
 *
 * Palette source: PALETTE_DAT (FILE0001, item 0 = handle 0x00010000).
 * That is the default game palette loaded by RAP_Init via GLB_LockItem.
 * It is 768 bytes: 256 * 3, each channel 0-63 (Watcom VGA DAC format).
 */
static void extract_sprites(const char *outdir, int total_items)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/sprites", outdir);
    if (make_dir(dir) != 0) return;

    /* Load the default palette: PALETTE_DAT = 0x00010000 */
    DWORD pal_handle = 0x00010000u;
    BYTE *pal6 = (BYTE *)GLB_GetItem(pal_handle);
    if (!pal6) {
        fprintf(stderr, "extract_sprites: failed to load PALETTE_DAT\n");
        return;
    }

    int written = 0;
    int skipped_non_linear = 0;
    int skipped_bad_dims = 0;
    int pic_seq = 0;  /* sequential counter to disambiguate duplicate names */

    for (int i = 0; i < total_items; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(i, name, sizeof name, &handle, &sz) != 0) continue;

        /* Only process items whose name ends in _PIC */
        if (!name_has_suffix(name, "_PIC")) continue;

        pic_seq++;

        /* Skip items with zero size (they are labels/stubs). */
        if (sz == 0) continue;

        /* Items must be at least as large as the GFX_PIC header. */
        if (sz < sizeof(GFX_PIC)) {
            fprintf(stderr, "[skip tiny] %s size=%zu\n", name, sz);
            continue;
        }

        BYTE *mem = (BYTE *)GLB_GetItem(handle);
        if (!mem) {
            fprintf(stderr, "[skip null] %s\n", name);
            continue;
        }

        GFX_PIC *pic = (GFX_PIC *)mem;

        if (pic->width <= 0 || pic->height <= 0 ||
            pic->width > 4096 || pic->height > 4096) {
            skipped_bad_dims++;
            GLB_FreeItem(handle);
            continue;
        }

        /* Rasterize to an indexed (palette-indexed) canvas. */
        uint8_t *canvas = (uint8_t *)calloc((size_t)pic->width * (size_t)pic->height, 1);
        if (!canvas) {
            fprintf(stderr, "[skip oom canvas] %s\n", name);
            GLB_FreeItem(handle);
            continue;
        }

        if (pic->type == GPIC) {
            /* Linear layout: pixel bytes start immediately after header. */
            size_t expected = sizeof(GFX_PIC) + (size_t)pic->width * (size_t)pic->height;
            if (sz < expected) {
                fprintf(stderr, "[skip truncated] %s sz=%zu expected=%zu\n",
                        name, sz, expected);
                free(canvas);
                GLB_FreeItem(handle);
                continue;
            }
            memcpy(canvas, mem + sizeof(GFX_PIC),
                   (size_t)pic->width * (size_t)pic->height);
        } else if (pic->type == GSPRITE) {
            /* Sprite format: sequence of (GFX_SPRITE header + length bytes)
             * tuples, terminated by a GFX_SPRITE whose offset field == ~0.
             * Fields: INT x, INT y, INT offset (sentinel), INT length.
             * Pixels at positions [seg_x .. seg_x+length) on row seg_y. */
            const BYTE *p = mem + sizeof(GFX_PIC);
            const BYTE *end = mem + sz;
            int w = pic->width, h = pic->height;
            for (int iter = 0; iter < 65536 && p + (int)sizeof(GFX_SPRITE) <= end; iter++) {
                INT seg_x, seg_y, seg_off, seg_len;
                memcpy(&seg_x,   p + 0,  sizeof(INT));
                memcpy(&seg_y,   p + 4,  sizeof(INT));
                memcpy(&seg_off, p + 8,  sizeof(INT));
                memcpy(&seg_len, p + 12, sizeof(INT));
                if ((unsigned int)seg_off == (unsigned int)~0) break;  /* EMPTY sentinel */
                p += sizeof(GFX_SPRITE);
                /* Clamp to canvas bounds before copying. */
                if (seg_y >= 0 && seg_y < h && seg_x >= 0 && seg_len > 0) {
                    int copy_len = seg_len;
                    if (seg_x + copy_len > w) copy_len = w - seg_x;
                    if (copy_len > 0 && p + copy_len <= end) {
                        memcpy(canvas + seg_y * w + seg_x, p, (size_t)copy_len);
                    }
                }
                if (p + seg_len > end) break;
                p += seg_len;
            }
        } else {
            fprintf(stdout, "[skip unknown type] %s type=%d\n", name, (int)pic->type);
            skipped_non_linear++;
            free(canvas);
            GLB_FreeItem(handle);
            continue;
        }

        uint8_t *rgba = NULL;
        indexed_to_rgba(canvas, pic->width, pic->height, pal6, &rgba);
        free(canvas);
        if (!rgba) {
            fprintf(stderr, "[skip oom] %s\n", name);
            GLB_FreeItem(handle);
            continue;
        }

        /* Use sequential index to avoid clobbering duplicate-named items. */
        char path[4096 + 80];
        snprintf(path, sizeof path, "%s/%04d_%s.png", dir, pic_seq, name);
        if (png_write_rgba(path, pic->width, pic->height, rgba) != 0)
            fprintf(stderr, "[write error] %s\n", name);
        else
            written++;

        free(rgba);
        GLB_FreeItem(handle);
    }

    GLB_FreeItem(pal_handle);

    fprintf(stdout,
            "extract_assets: dumped %d sprites "
            "(%d skipped unknown type, %d bad dims)\n",
            written, skipped_non_linear, skipped_bad_dims);
}

static void usage(const char *me) {
    fprintf(stderr,
        "usage: %s <FILE0000.GLB> <FILE0001.GLB> <output_dir>\n", me);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc != 4) usage(argv[0]);

    const char *glb0   = argv[1];
    const char *glb1   = argv[2];
    const char *outdir = argv[3];

    /*
     * GLB_InitSystem discovers archives by constructing names like
     * FILE0000.glb, FILE0001.glb in either the CWD or the directory
     * derived from exepath (by stripping after the last '\').
     * On POSIX the backslash search never fires, so the whole exepath
     * string is used as a prefix — which would corrupt the constructed
     * filename.  The reliable fix is to chdir to the GLB directory
     * before calling GLB_InitSystem.
     *
     * We derive the directory from glb0; both archives are assumed to
     * live in the same directory (standard Raptor layout).
     */
    {
        /* dirname() may modify its argument; work on a copy. */
        char glb0_copy[4096];
        snprintf(glb0_copy, sizeof glb0_copy, "%s", glb0);
        const char *glb_dir = dirname(glb0_copy);
        if (chdir(glb_dir) != 0) {
            perror("chdir to GLB directory");
            return 1;
        }
    }

    /*
     * Open both archives.  innum=2 tells GLB_InitSystem to look for
     * FILE0000.glb (filenum=0) and FILE0001.glb (filenum=1).
     * Returns the count of archives actually opened.
     */
    int opened = GLB_InitSystem(argv[0], 2, NULL);
    if (opened < 1) {
        fprintf(stderr, "GLB_InitSystem: no archives opened (opened=%d)\n",
                opened);
        return 1;
    }

    int n0 = GLB_GetFileItems(0);
    int n1 = GLB_GetFileItems(1);

    fprintf(stdout, "Loaded %s: %d items\n", glb0, n0);
    fprintf(stdout, "Loaded %s: %d items\n", glb1, n1);

    if (n0 <= 0 || n1 <= 0) {
        fprintf(stderr,
            "Warning: one or more archives reported 0 items "
            "(n0=%d, n1=%d)\n", n0, n1);
    }

    /* Smoke test: print first 5 items from each archive. */
    fprintf(stdout, "\nFirst items in FILE0000.GLB:\n");
    for (int i = 0; i < 5 && i < n0; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(i, name, sizeof name, &handle, &sz) == 0) {
            fprintf(stdout, "  [%d] name=%-16s handle=0x%08lx size=%zu\n",
                    i, name, (unsigned long)handle, sz);
        }
    }
    fprintf(stdout, "\nFirst items in FILE0001.GLB:\n");
    for (int i = 0; i < 5 && i < n1; i++) {
        char name[64];
        DWORD handle;
        size_t sz;
        if (GLB_GetItemInfo(n0 + i, name, sizeof name, &handle, &sz) == 0) {
            fprintf(stdout, "  [%d] name=%-16s handle=0x%08lx size=%zu\n",
                    n0 + i, name, (unsigned long)handle, sz);
        }
    }

    /* PNG writer smoke test: write a 4x4 RGBA checkerboard to /tmp. */
    {
        uint8_t checker[4 * 4 * 4];
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                int p = (y * 4 + x) * 4;
                int white = (x + y) % 2;
                checker[p + 0] = white ? 255 : 0;    /* R */
                checker[p + 1] = white ? 255 : 0;    /* G */
                checker[p + 2] = white ? 255 : 0;    /* B */
                checker[p + 3] = white ? 255 : 128;  /* A */
            }
        }
        if (png_write_rgba("/tmp/checker.png", 4, 4, checker) == 0)
            fprintf(stdout, "PNG smoke test: /tmp/checker.png written OK\n");
        else
            fprintf(stderr, "PNG smoke test: FAILED\n");
    }

    /* Create output directory. */
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir output directory");
        return 1;
    }

    int total_items = n0 + n1;

    /* Extract _PIC items as PNG files. */
    extract_sprites(outdir, total_items);

    /* Extract _REC (demo recording) items as JSON files. */
    dump_demo_items(outdir);

    /* Extract _MAP (MAZELEVEL) items as JSON files. */
    dump_level_items(outdir);

    /* Extract _ITM (SPRITE struct array) items as JSON files. */
    dump_sprite_meta_items(outdir);

    return 0;
}
