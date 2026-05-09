/*
 * sprite_meta_dumper.c - Extract Raptor SPRITE struct arrays (_ITM items) as JSON.
 *
 * SPRITE struct (from SOURCE/MAP.H), field sizes from DOS/386 Watcom C:
 *   All INT fields are 4 bytes (32-bit protected mode).
 *   All SHORT fields are 2 bytes.
 *   All BOOL fields are INT (4 bytes) in Watcom.
 *   MAX_GUNS  = 24
 *   MAX_FLIGHT = 30
 *
 * Item names follow SPRITE1_ITM, SPRITE2_ITM, SPRITE3_ITM, SPRITE4_ITM
 * (one per game, see SOURCE/ENEMY.C and SOURCE/FILE0001.INC).
 *
 * Each _ITM item is a contiguous array of SPRITE records.
 * Size of one SPRITE record (computed with packed layout):
 *   iname[16]:       16 bytes
 *   item (DWORD):     4 bytes
 *   bonus (INT):      4 bytes
 *   exptype (INT):    4 bytes  (enum, underlying int)
 *   shotspace (INT):  4 bytes
 *   ground (INT):     4 bytes  (BOOL = INT in Watcom)
 *   suck (INT):       4 bytes
 *   frame_rate (INT): 4 bytes
 *   num_frames (INT): 4 bytes
 *   countdown (INT):  4 bytes
 *   rewind (INT):     4 bytes
 *   animtype (INT):   4 bytes  (enum)
 *   shadow (INT):     4 bytes
 *   bossflag (INT):   4 bytes
 *   hits (INT):       4 bytes
 *   money (INT):      4 bytes
 *   shootstart (INT): 4 bytes
 *   shootcnt (INT):   4 bytes
 *   shootframe (INT): 4 bytes
 *   movespeed (INT):  4 bytes
 *   numflight (INT):  4 bytes
 *   repos (INT):      4 bytes
 *   flighttype (INT): 4 bytes  (enum)
 *   numguns (INT):    4 bytes
 *   numengs (INT):    4 bytes
 *   sfx (INT):        4 bytes
 *   song (INT):       4 bytes
 *   = 27 scalars so far, plus 16-byte iname + 4-byte item
 *   shoot_type[24]:  24 * 2 = 48 bytes
 *   engx[24]:        24 * 2 = 48 bytes
 *   engy[24]:        24 * 2 = 48 bytes
 *   englx[24]:       24 * 2 = 48 bytes
 *   shootx[24]:      24 * 2 = 48 bytes
 *   shooty[24]:      24 * 2 = 48 bytes
 *   flightx[30]:     30 * 2 = 60 bytes
 *   flighty[30]:     30 * 2 = 60 bytes
 *
 * Total scalar ints: iname(16) + item(4) + 26*int(104) = 124 bytes
 * Arrays: 6*24*2 + 2*30*2 = 288 + 120 = 408 bytes
 * Grand total: 120 + 408 = 528 bytes per SPRITE
 *
 * Output: <outdir>/sprites_meta/<name>.json
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#include "sprite_meta_dumper.h"
#include "GLBAPI.H"

#define MAX_GUNS   24
#define MAX_FLIGHT 30

/* Packed SPRITE struct matching DOS/386 binary layout exactly. */
typedef struct {
    char     iname[16];
    uint32_t item;
    int32_t  bonus;
    int32_t  exptype;
    int32_t  shotspace;
    int32_t  ground;
    int32_t  suck;
    int32_t  frame_rate;
    int32_t  num_frames;
    int32_t  countdown;
    int32_t  rewind;
    int32_t  animtype;
    int32_t  shadow;
    int32_t  bossflag;
    int32_t  hits;
    int32_t  money;
    int32_t  shootstart;
    int32_t  shootcnt;
    int32_t  shootframe;
    int32_t  movespeed;
    int32_t  numflight;
    int32_t  repos;
    int32_t  flighttype;
    int32_t  numguns;
    int32_t  numengs;
    int32_t  sfx;
    int32_t  song;
    int16_t  shoot_type[MAX_GUNS];
    int16_t  engx[MAX_GUNS];
    int16_t  engy[MAX_GUNS];
    int16_t  englx[MAX_GUNS];
    int16_t  shootx[MAX_GUNS];
    int16_t  shooty[MAX_GUNS];
    int16_t  flightx[MAX_FLIGHT];
    int16_t  flighty[MAX_FLIGHT];
} __attribute__((packed)) SpriteRecord;

_Static_assert(sizeof(SpriteRecord) == 528, "SpriteRecord must be 528 bytes");

static int has_suffix(const char *name, const char *suffix)
{
    size_t nlen = strlen(name);
    size_t slen = strlen(suffix);
    if (slen > nlen) return 0;
    return (strncasecmp(name + nlen - slen, suffix, slen) == 0);
}

/* Emit a JSON int16 array. */
static void emit_short_array(FILE *fp, const char *key,
                             const int16_t *arr, int count, int trailing_comma)
{
    fprintf(fp, "    \"%s\": [", key);
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d%s", (int)arr[i], i < count - 1 ? ", " : "");
    }
    fprintf(fp, "]%s\n", trailing_comma ? "," : "");
}

/* Emit a safe JSON string (printable ASCII only). */
static void emit_json_string(FILE *fp, const char *s, size_t maxlen)
{
    fputc('"', fp);
    for (size_t i = 0; i < maxlen && s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"')       fputs("\\\"", fp);
        else if (c == '\\') fputs("\\\\", fp);
        else if (c >= 0x20 && c < 0x7f) fputc(c, fp);
        /* skip non-printable bytes */
    }
    fputc('"', fp);
}

int dump_sprite_meta_items(const char *outdir)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/sprites_meta", outdir);
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

        /* SPRITE struct arrays use the _ITM suffix, but so do FLATS arrays
         * (FLATSG1_ITM, etc.).  Only process items whose name starts with
         * "SPRITE" — these are the enemy/ship sprite definition libraries. */
        if (!has_suffix(name, "_ITM")) continue;
        if (strncasecmp(name, "SPRITE", 6) != 0) continue;

        if (sz == 0 || sz < sizeof(SpriteRecord)) {
            fprintf(stderr, "[sprite_meta skip tiny] %s size=%zu\n", name, sz);
            continue;
        }

        BYTE *mem = GLB_GetItem(handle);
        if (!mem) {
            fprintf(stderr, "[sprite_meta skip null] %s\n", name);
            continue;
        }

        int num_sprites = (int)(sz / sizeof(SpriteRecord));

        char path[4096 + 80];
        snprintf(path, sizeof path, "%s/%s.json", dir, name);

        FILE *fp = fopen(path, "w");
        if (!fp) {
            fprintf(stderr, "[sprite_meta open error] %s: %s\n",
                    path, strerror(errno));
            GLB_FreeItem(handle);
            continue;
        }

        SpriteRecord *recs = (SpriteRecord *)mem;

        fprintf(fp, "{\n");
        fprintf(fp, "  \"name\": \"%s\",\n", name);
        fprintf(fp, "  \"num_sprites\": %d,\n", num_sprites);
        fprintf(fp, "  \"sprites\": [\n");

        for (int s = 0; s < num_sprites; s++) {
            SpriteRecord *r = &recs[s];
            int is_last = (s == num_sprites - 1);

            fprintf(fp, "  {\n");
            fprintf(fp, "    \"iname\": ");
            emit_json_string(fp, r->iname, sizeof(r->iname));
            fprintf(fp, ",\n");
            fprintf(fp, "    \"item\": %u,\n",       (unsigned)r->item);
            fprintf(fp, "    \"bonus\": %d,\n",       (int)r->bonus);
            fprintf(fp, "    \"exptype\": %d,\n",     (int)r->exptype);
            fprintf(fp, "    \"shotspace\": %d,\n",   (int)r->shotspace);
            fprintf(fp, "    \"ground\": %d,\n",      (int)r->ground);
            fprintf(fp, "    \"suck\": %d,\n",        (int)r->suck);
            fprintf(fp, "    \"frame_rate\": %d,\n",  (int)r->frame_rate);
            fprintf(fp, "    \"num_frames\": %d,\n",  (int)r->num_frames);
            fprintf(fp, "    \"countdown\": %d,\n",   (int)r->countdown);
            fprintf(fp, "    \"rewind\": %d,\n",      (int)r->rewind);
            fprintf(fp, "    \"animtype\": %d,\n",    (int)r->animtype);
            fprintf(fp, "    \"shadow\": %d,\n",      (int)r->shadow);
            fprintf(fp, "    \"bossflag\": %d,\n",    (int)r->bossflag);
            fprintf(fp, "    \"hits\": %d,\n",        (int)r->hits);
            fprintf(fp, "    \"money\": %d,\n",       (int)r->money);
            fprintf(fp, "    \"shootstart\": %d,\n",  (int)r->shootstart);
            fprintf(fp, "    \"shootcnt\": %d,\n",    (int)r->shootcnt);
            fprintf(fp, "    \"shootframe\": %d,\n",  (int)r->shootframe);
            fprintf(fp, "    \"movespeed\": %d,\n",   (int)r->movespeed);
            fprintf(fp, "    \"numflight\": %d,\n",   (int)r->numflight);
            fprintf(fp, "    \"repos\": %d,\n",       (int)r->repos);
            fprintf(fp, "    \"flighttype\": %d,\n",  (int)r->flighttype);
            fprintf(fp, "    \"numguns\": %d,\n",     (int)r->numguns);
            fprintf(fp, "    \"numengs\": %d,\n",     (int)r->numengs);
            fprintf(fp, "    \"sfx\": %d,\n",         (int)r->sfx);
            fprintf(fp, "    \"song\": %d,\n",        (int)r->song);
            emit_short_array(fp, "shoot_type", r->shoot_type, MAX_GUNS, 1);
            emit_short_array(fp, "engx",       r->engx,       MAX_GUNS, 1);
            emit_short_array(fp, "engy",       r->engy,       MAX_GUNS, 1);
            emit_short_array(fp, "englx",      r->englx,      MAX_GUNS, 1);
            emit_short_array(fp, "shootx",     r->shootx,     MAX_GUNS, 1);
            emit_short_array(fp, "shooty",     r->shooty,     MAX_GUNS, 1);
            emit_short_array(fp, "flightx",    r->flightx,    MAX_FLIGHT, 1);
            emit_short_array(fp, "flighty",    r->flighty,    MAX_FLIGHT, 0);
            fprintf(fp, "  }%s\n", is_last ? "" : ",");
        }

        fprintf(fp, "  ]\n");
        fprintf(fp, "}\n");

        fclose(fp);
        GLB_FreeItem(handle);
        written++;
        fprintf(stdout, "sprite_meta: wrote %s (%d sprites, sz=%zu)\n",
                path, num_sprites, sz);
    }

    fprintf(stdout, "extract_assets: dumped %d sprite_meta files\n", written);
    return written;
}
