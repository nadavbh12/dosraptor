/*
 * wav_writer.c - Extract Raptor DMX digital sound patches to WAV.
 *
 * Sound items in the GLB archives use the Apogee DMX patch format.
 * Item names end in _FX; each _FX label covers up to 5 sequential items:
 *   label+0: PC-speaker data (type 0)
 *   label+1: AdLib FM data  (type 1)
 *   label+2: MIDI channel   (type 2)
 *   label+3: GUS patch      (type 3, same type byte as digital — see below)
 *   label+4: Sound Blaster digital PCM (type 3)
 *
 * The DMX type discriminator is in bytes [0..1] of the item data:
 *   type == 0: PC speaker data (dmxpcs_t)
 *   type == 3: digital PCM patch
 *
 * Digital PCM patch layout (from apodmx/DMX.C SFX_PlayPatch):
 *   uint16_t type;          -- bytes 0-1: == 3
 *   uint16_t sample_rate;   -- bytes 2-3: Hz (typically 11025)
 *   uint32_t data_length;   -- bytes 4-7: total byte count including header
 *   uint8_t  pad[16];       -- bytes 8-23: padding (unused)
 *   uint8_t  samples[];     -- bytes 24..(data_length-9): unsigned 8-bit PCM
 *     actual sample count = data_length - 32
 *
 * We detect all items whose first 2 bytes read as type==3 and whose size
 * is consistent with a digital patch. This naturally catches all _DSP items
 * (digital sound patches) regardless of which suffix group they belong to.
 *
 * We convert unsigned 8-bit PCM (0-255, center=128) to signed 16-bit PCM
 * (standard WAV format) for maximum compatibility.
 *
 * Output: <outdir>/sounds/<name>.wav
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

#include "wav_writer.h"
#include "GLBAPI.H"

/* RIFF/WAVE header for PCM audio. All multi-byte fields are little-endian. */
static void write_le16(FILE *fp, uint16_t v)
{
    unsigned char buf[2] = { (unsigned char)(v & 0xff),
                             (unsigned char)((v >> 8) & 0xff) };
    fwrite(buf, 1, 2, fp);
}

static void write_le32(FILE *fp, uint32_t v)
{
    unsigned char buf[4] = { (unsigned char)(v & 0xff),
                             (unsigned char)((v >> 8) & 0xff),
                             (unsigned char)((v >> 16) & 0xff),
                             (unsigned char)((v >> 24) & 0xff) };
    fwrite(buf, 1, 4, fp);
}

static void write_wav_header(FILE *fp, uint32_t sample_rate,
                             uint16_t num_channels, uint16_t bits_per_sample,
                             uint32_t num_samples)
{
    uint32_t byte_rate   = sample_rate * num_channels * (bits_per_sample / 8);
    uint16_t block_align = (uint16_t)(num_channels * (bits_per_sample / 8));
    uint32_t data_size   = num_samples * num_channels * (bits_per_sample / 8);
    uint32_t riff_size   = 36 + data_size; /* header (44) - 8 bytes RIFF chunk header */

    fwrite("RIFF", 1, 4, fp);
    write_le32(fp, riff_size);
    fwrite("WAVE", 1, 4, fp);

    /* fmt  chunk */
    fwrite("fmt ", 1, 4, fp);
    write_le32(fp, 16);              /* chunk size for PCM */
    write_le16(fp, 1);               /* audio format: PCM */
    write_le16(fp, num_channels);
    write_le32(fp, sample_rate);
    write_le32(fp, byte_rate);
    write_le16(fp, block_align);
    write_le16(fp, bits_per_sample);

    /* data chunk */
    fwrite("data", 1, 4, fp);
    write_le32(fp, data_size);
}

/* Minimum size of a valid digital patch: 24-byte header + at least 1 sample */
#define DMX_DIGITAL_HDR_SIZE  24
#define DMX_DIGITAL_LEN_BIAS  32  /* data_length field includes this many bytes */
#define DMX_TYPE_DIGITAL      3

/*
 * is_digital_patch() - returns 1 if the item looks like a DMX digital patch.
 * Requirements:
 *   - size >= DMX_DIGITAL_HDR_SIZE
 *   - bytes[0..1] == 3 (little-endian uint16)
 *   - data_length field >= 32 (consistent)
 *   - actual sample count (data_length - 32) <= item size - 24
 */
static int is_digital_patch(const unsigned char *data, size_t sz,
                             uint16_t *out_rate, uint32_t *out_nsamples)
{
    if (sz < DMX_DIGITAL_HDR_SIZE) return 0;

    uint16_t type = (uint16_t)(data[0] | (data[1] << 8));
    if (type != DMX_TYPE_DIGITAL) return 0;

    uint16_t rate    = (uint16_t)(data[2] | (data[3] << 8));
    uint32_t datalen = (uint32_t)(data[4] | (data[5] << 8) |
                                  (data[6] << 16) | (data[7] << 24));

    if (datalen < (uint32_t)DMX_DIGITAL_LEN_BIAS) return 0;
    uint32_t nsamples = datalen - (uint32_t)DMX_DIGITAL_LEN_BIAS;

    /* Sanity: sample data must fit within the item. */
    if ((size_t)nsamples > sz - DMX_DIGITAL_HDR_SIZE) return 0;

    /* Reject obviously bogus sample rates. */
    if (rate < 4000 || rate > 48000) return 0;

    if (out_rate)    *out_rate    = rate;
    if (out_nsamples) *out_nsamples = nsamples;
    return 1;
}

/*
 * Sound item discovery strategy:
 *
 * Each _FX LABEL (size=0) is followed by exactly 4 data items with empty names:
 *   LABEL+0: size=0  (the LABEL itself, name=XXX_FX)
 *   LABEL+1: PC speaker data (type=0, small)
 *   LABEL+2: AdLib FM data (small)
 *   LABEL+3: MIDI / GUS data (small or medium)
 *   LABEL+4: Sound Blaster digital PCM (type=3, large, the one we want)
 *
 * We detect FX groups by finding _FX LABEL items (size=0, name ends in _FX),
 * then extract the digital PCM item at LABEL_index+4.
 * Fallback: also scan all items for type=3 digital patches regardless of name.
 */

/* Checks if a name ends with _FX (case-insensitive). */
static int name_ends_with_fx(const char *name)
{
    size_t nlen = strlen(name);
    if (nlen < 3) return 0;
    return (strncasecmp(name + nlen - 3, "_FX", 3) == 0);
}

int dump_sound_items(const char *outdir)
{
    char dir[4096];
    snprintf(dir, sizeof dir, "%s/sounds", outdir);
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
    int skipped_type = 0;

    for (int i = 0; i < total; i++) {
        char label_name[64];
        DWORD label_handle;
        size_t label_sz;
        if (GLB_GetItemInfo(i, label_name, sizeof label_name,
                            &label_handle, &label_sz) != 0) continue;

        /* Only process _FX LABEL items (size == 0, name ends in _FX). */
        if (label_sz != 0) continue;
        if (!name_ends_with_fx(label_name)) continue;

        /* The digital patch is 4 items after the LABEL. */
        int digital_idx = i + 4;
        if (digital_idx >= total) continue;

        char item_name[64];
        DWORD item_handle;
        size_t item_sz;
        if (GLB_GetItemInfo(digital_idx, item_name, sizeof item_name,
                            &item_handle, &item_sz) != 0) continue;

        if (item_sz < DMX_DIGITAL_HDR_SIZE) {
            skipped_type++;
            continue;
        }

        BYTE *mem = GLB_GetItem(item_handle);
        if (!mem) continue;

        uint16_t rate;
        uint32_t nsamples;
        if (!is_digital_patch((const unsigned char *)mem, item_sz, &rate, &nsamples)) {
            skipped_type++;
            GLB_FreeItem(item_handle);
            continue;
        }

        if (nsamples == 0) {
            GLB_FreeItem(item_handle);
            continue;
        }

        /* Derive the label name without the _FX suffix for the filename,
         * then add _FX back for clarity, e.g. "EXPLO_FX.wav". */
        char path[4096 + 80];
        snprintf(path, sizeof path, "%s/%s.wav", dir, label_name);

        FILE *fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "[sound open error] %s: %s\n",
                    path, strerror(errno));
            GLB_FreeItem(item_handle);
            continue;
        }

        /* Convert unsigned 8-bit to signed 16-bit PCM.
         * DMX digital: unsigned 8-bit (0=silence/min, 128=center).
         * WAV PCM 16-bit: signed (0=center). */
        write_wav_header(fp, rate, 1 /* mono */, 16, nsamples);

        const unsigned char *samples =
            (const unsigned char *)mem + DMX_DIGITAL_HDR_SIZE;
        for (uint32_t s = 0; s < nsamples; s++) {
            /* u8 -> s16: subtract 128 to center, scale to s16 range */
            int16_t sample = (int16_t)((int)samples[s] - 128) * 256;
            write_le16(fp, (uint16_t)sample);
        }

        fclose(fp);
        GLB_FreeItem(item_handle);
        written++;
        fprintf(stdout, "sound: wrote %s (rate=%u Hz, nsamples=%u)\n",
                path, (unsigned)rate, (unsigned)nsamples);
    }

    if (skipped_type > 0)
        fprintf(stdout,
                "sound: skipped %d FX groups with no valid digital patch\n",
                skipped_type);
    fprintf(stdout, "extract_assets: dumped %d sound WAV files\n", written);
    return written;
}
