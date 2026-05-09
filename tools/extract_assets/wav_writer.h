#ifndef WAV_WRITER_H
#define WAV_WRITER_H

/* dump_sound_items() - extract all DMX digital sound patches (_FX items
 * that contain type-3 digital PCM data) as WAV files.
 *
 * Sound items are stored in Doom/Apogee DMX patch format:
 *   uint16 type (3 = digital PCM)
 *   uint16 sample_rate (Hz)
 *   uint32 data_length  (total byte count INCLUDING a 32-byte header)
 *   ... 16 bytes padding ...
 *   uint8  samples[data_length - 32]  (unsigned 8-bit PCM, mono)
 *
 * Output: <outdir>/sounds/<name>.wav, one file per digital patch.
 * Returns the number of WAV files written. */
int dump_sound_items(const char *outdir);

#endif /* WAV_WRITER_H */
