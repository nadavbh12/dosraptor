#ifndef PNG_WRITER_H
#define PNG_WRITER_H

#include <stddef.h>
#include <stdint.h>

/* Write 8-bit RGBA PNG.  pixels is width*height*4 bytes (RGBA tightly packed).
 * Returns 0 on success, -1 on I/O or libpng error. */
int png_write_rgba(const char *path, int width, int height,
                   const uint8_t *pixels);

/* Convert indexed (256-color) image with 6-bit-per-channel Watcom DAC palette
 * (pal_6bit: 256*3 bytes, R/G/B each 0-63) to RGBA8888.
 * Color index 0 is rendered fully transparent (alpha=0).
 * All other indices are fully opaque (alpha=255).
 * 6-to-8 bit scaling: out = (v << 2) | (v >> 4).
 * *out_rgba is malloc'd by this function; caller must free() it. */
void indexed_to_rgba(const uint8_t *indexed, int width, int height,
                     const uint8_t *pal_6bit, uint8_t **out_rgba);

#endif /* PNG_WRITER_H */
