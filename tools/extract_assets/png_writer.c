#include "png_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

/*---------------------------------------------------------------------------
 * indexed_to_rgba
 *
 * Converts an 8-bit indexed (paletted) image to 32-bit RGBA.
 * pal_6bit: 256 * 3 bytes, each channel is 0-63 (Watcom VGA DAC layout).
 * Color index 0 is transparent; all others are fully opaque.
 * 6→8 bit: out = (v << 2) | (v >> 4)
 *---------------------------------------------------------------------------*/
void indexed_to_rgba(const uint8_t *indexed, int width, int height,
                     const uint8_t *pal_6bit, uint8_t **out_rgba)
{
    int npixels = width * height;
    uint8_t *rgba = (uint8_t *)malloc((size_t)npixels * 4);
    if (!rgba) {
        *out_rgba = NULL;
        return;
    }

    /* Pre-expand the 6-bit palette to 8-bit RGBA (256 entries). */
    uint8_t pal8[256 * 4];
    for (int i = 0; i < 256; i++) {
        uint8_t r6 = pal_6bit[i * 3 + 0];
        uint8_t g6 = pal_6bit[i * 3 + 1];
        uint8_t b6 = pal_6bit[i * 3 + 2];
        pal8[i * 4 + 0] = (uint8_t)((r6 << 2) | (r6 >> 4));
        pal8[i * 4 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
        pal8[i * 4 + 2] = (uint8_t)((b6 << 2) | (b6 >> 4));
        pal8[i * 4 + 3] = (i == 0) ? 0 : 255;  /* index 0 = transparent */
    }

    for (int p = 0; p < npixels; p++) {
        uint8_t idx = indexed[p];
        rgba[p * 4 + 0] = pal8[idx * 4 + 0];
        rgba[p * 4 + 1] = pal8[idx * 4 + 1];
        rgba[p * 4 + 2] = pal8[idx * 4 + 2];
        rgba[p * 4 + 3] = pal8[idx * 4 + 3];
    }

    *out_rgba = rgba;
}

/*---------------------------------------------------------------------------
 * png_write_rgba
 *
 * Writes a width x height RGBA8888 image to the file at `path`.
 * Returns 0 on success, -1 on any error.
 *---------------------------------------------------------------------------*/
int png_write_rgba(const char *path, int width, int height,
                   const uint8_t *pixels)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "png_write_rgba: cannot open %s\n", path);
        return -1;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        fclose(fp);
        return -1;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)width, (png_uint_32)height,
                 8,                      /* bit depth */
                 PNG_COLOR_TYPE_RGBA,    /* color type */
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    /* Write row by row. */
    for (int y = 0; y < height; y++) {
        png_write_row(png, (png_const_bytep)(pixels + (size_t)y * (size_t)width * 4));
    }

    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}
