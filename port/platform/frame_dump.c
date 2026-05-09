// port/platform/frame_dump.c
//
// 24-bit BMP screenshot writer. The output format is uncompressed BGR,
// bottom-up, no row padding (320*3 = 960 is already a multiple of 4).
// macOS Preview, VS Code, and every browser open it directly — no PNG
// dependency needed.

#include "frame_dump.h"
#include "gfx_sdl.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern unsigned char *displaybuffer;
extern unsigned char *displayscreen;

#define LOGICAL_W 320
#define LOGICAL_H 200

static int           g_enabled;
static char          g_dir[1024];
static int           g_every;
static int           g_dump_on_key;
static unsigned long g_seq;
static unsigned long g_present_count;

static void mkdir_p(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return;

    char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

void raptor_dump_init(void)
{
    if (g_enabled) return;

    const char *dir = getenv("RAPTOR_DUMP_DIR");
    if (!dir || !*dir) dir = "dumps";
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    mkdir_p(g_dir);

    const char *every = getenv("RAPTOR_DUMP_EVERY");
    g_every = every ? atoi(every) : 0;
    if (g_every < 0) g_every = 0;

    const char *key = getenv("RAPTOR_DUMP_KEY");
    g_dump_on_key = (key && *key && *key != '0') ? 1 : 0;

    g_enabled = 1;

    fprintf(stdout, "raptor_dump: dir=%s every=%d f12=%s\n",
            g_dir, g_every, g_dump_on_key ? "on" : "off");
    fflush(stdout);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void write_bmp(const char *path, const uint8_t *fb,
                      const uint32_t *pal_lut)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "raptor_dump: fopen(%s): %s\n",
                path, strerror(errno));
        return;
    }

    const uint32_t pix_size  = (uint32_t)(LOGICAL_W * LOGICAL_H * 3);
    const uint32_t file_size = 14 + 40 + pix_size;

    uint8_t hdr[14] = { 'B', 'M' };
    put_u32_le(hdr + 2,  file_size);
    put_u32_le(hdr + 6,  0);
    put_u32_le(hdr + 10, 14 + 40);
    fwrite(hdr, 1, 14, f);

    uint8_t info[40] = { 0 };
    put_u32_le(info +  0, 40);
    put_u32_le(info +  4, LOGICAL_W);
    put_u32_le(info +  8, LOGICAL_H);
    info[12] = 1;   info[13] = 0;     // planes
    info[14] = 24;  info[15] = 0;     // bits per pixel
    put_u32_le(info + 16, 0);         // BI_RGB
    put_u32_le(info + 20, pix_size);
    put_u32_le(info + 24, 2835);      // 72 DPI horizontal
    put_u32_le(info + 28, 2835);      // 72 DPI vertical
    fwrite(info, 1, 40, f);

    uint8_t row[LOGICAL_W * 3];
    for (int y = LOGICAL_H - 1; y >= 0; y--) {
        const uint8_t *src = fb + y * LOGICAL_W;
        for (int x = 0; x < LOGICAL_W; x++) {
            uint32_t argb = pal_lut[src[x]];
            row[x * 3 + 0] = (uint8_t)(argb);          // B
            row[x * 3 + 1] = (uint8_t)(argb >> 8);     // G
            row[x * 3 + 2] = (uint8_t)(argb >> 16);    // R
        }
        fwrite(row, 1, sizeof row, f);
    }
    fclose(f);
}

static void sanitize_label(const char *in, char *out, size_t n)
{
    if (!in || !*in) in = "frame";
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < n; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')
            out[j++] = c;
        else
            out[j++] = '_';
    }
    out[j] = '\0';
}

void raptor_dump_frame(const char *label)
{
    if (!g_enabled) raptor_dump_init();
    /* Prefer displayscreen — that's the post-update copy that includes
     * cursor sprites + any other "painted-after-game-render" overlays.
     * displaybuffer has the cursor erased back out. Fall back to
     * displaybuffer if displayscreen isn't allocated yet (early init). */
    const unsigned char *src = displayscreen ? displayscreen : displaybuffer;
    if (!src) return;

    const uint32_t *pal = gfx_sdl_palette_lut();
    if (!pal) return;

    char safe[64];
    sanitize_label(label, safe, sizeof safe);

    char path[1280];
    snprintf(path, sizeof path, "%s/%05lu_%s.bmp",
             g_dir, ++g_seq, safe);
    write_bmp(path, src, pal);

    fprintf(stdout, "raptor_dump: %s\n", path);
    fflush(stdout);
}

void raptor_dump_on_present(void)
{
    if (!g_enabled || g_every <= 0) return;
    if ((g_present_count++ % (unsigned long)g_every) != 0) return;
    raptor_dump_frame("auto");
}

void raptor_dump_keypress(void)
{
    if (!g_enabled || !g_dump_on_key) return;
    raptor_dump_frame("key");
}
