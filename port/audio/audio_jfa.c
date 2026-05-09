// port/audio/audio_jfa.c
//
// SDL2_mixer-backed audio. The 12 MUS_/SFX_ entrypoints from apodmx
// (RegisterSong, UnregisterSong, PlaySong, StopSong, ChainSong,
//  FadeInSong, FadeOutSong, QrySongPlaying, SetMasterVolume,
//  PlayPatch, StopPatch, Playing) are implemented here on top of
// Mix_PlayMusic/Mix_PlayChannel.
//
// Pipeline:
//   .GLB -> raw bytes ->
//     MUS  (id Software): mus2mid in apodmx/MUS2MID.C converts to MIDI,
//          Mix_LoadMUS_RW -> Mix_PlayMusic. SoundFont is fluid-synth's
//          fallback (TimGM6mb.sf2 next to the binary).
//     VOC  (Creative DMX): SFX_PlayPatch parses the apodmx PCM patch
//          format (rate at +2, length at +4, samples at +24) and
//          Mix_QuickLoad_RAW -> Mix_PlayChannel.

#include <SDL.h>
#include <SDL_mixer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* apodmx/MUS2MID.H declares `int mus2mid(...)` but the .C body returns
 * bool (gnu99). Use the bool-returning prototype to match the
 * implementation; success is FALSE = 0. */
#include <stdbool.h>
extern bool mus2mid(FILE *musinput, FILE *midioutput, int rate, int adlibhack);

// ---- DMX_Init / DeInit -------------------------------------------------
static int g_audio_ready = 0;

int DMX_Init(int rate, int voices, int mcard, int dcard)
{
    (void)rate; (void)voices; (void)mcard; (void)dcard;

    if (g_audio_ready) return 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "[audio] SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
        return -1;
    }

    int mix_flags = MIX_INIT_MID;
    int got = Mix_Init(mix_flags);
    if ((got & MIX_INIT_MID) == 0) {
        fprintf(stderr, "[audio] Mix_Init MID missing: %s\n", Mix_GetError());
        // fluid-synth or timidity should be present via the brew bottle;
        // fall through anyway — SFX still works.
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        fprintf(stderr, "[audio] Mix_OpenAudio: %s\n", Mix_GetError());
        return -1;
    }

    Mix_AllocateChannels(16);

    // Tell fluid-synth where to find a General MIDI SoundFont. Looked up
    // first relative to cwd (which sys_main chdirs to the asset dir),
    // then alongside the executable.
    if (access("TimGM6mb.sf2", F_OK) == 0) {
        Mix_SetSoundFonts("TimGM6mb.sf2");
        fprintf(stdout, "[audio] SoundFont: ./TimGM6mb.sf2\n");
    } else {
        fprintf(stdout, "[audio] no SoundFont found — MIDI may be silent\n");
    }

    g_audio_ready = 1;
    fprintf(stdout, "[audio] init ok (44.1kHz stereo, 16 chans)\n");
    /* apodmx DMX_Init returns nonzero on success (logical OR of music
     * + sfx init). FX.C disables audio if DMX_Init returns 0. */
    return 1;
}

void DMX_DeInit(void)
{
    if (!g_audio_ready) return;
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    Mix_CloseAudio();
    Mix_Quit();
    g_audio_ready = 0;
}

// ---- music handle table ------------------------------------------------
//
// Game stores the int returned by MUS_RegisterSong as cur_song_id and
// later passes it to the other MUS_* calls. We map it to a slot.

#define MUS_MAX_SLOTS 8

typedef struct {
    int        in_use;
    Mix_Music *music;
    char       midi_path[64];   // empty if music was loaded from RW
} mus_slot_t;

static mus_slot_t g_slots[MUS_MAX_SLOTS];
static int        g_current_slot = -1;

static int alloc_slot(void)
{
    for (int i = 1; i < MUS_MAX_SLOTS; i++)   // skip 0 so 0 means "no song"
        if (!g_slots[i].in_use) return i;
    return -1;
}

static void free_slot(int slot)
{
    if (slot <= 0 || slot >= MUS_MAX_SLOTS) return;
    if (g_slots[slot].music) {
        if (g_current_slot == slot) Mix_HaltMusic();
        Mix_FreeMusic(g_slots[slot].music);
    }
    if (g_slots[slot].midi_path[0]) unlink(g_slots[slot].midi_path);
    memset(&g_slots[slot], 0, sizeof g_slots[slot]);
}

// ---- MUS_RegisterSong --------------------------------------------------
//
// Original DMX: detect MThd (raw MIDI) vs MUS, run mus2mid if needed,
// hand back a handle. We preserve the contract: given a flat byte buffer
// from the GLB, return a non-zero slot id on success.
//
// MUS header layout (from id Software):
//   0..3   "MUS\x1A"
//   4..5   score length (uint16, LE)
//   6..7   score start offset (uint16, LE)
// Total bytes = score_start + score_length, as the apodmx ref does.

int MUS_RegisterSong(void *data)
{
    if (!g_audio_ready || !data) return 0;

    const unsigned char *bytes = (const unsigned char *)data;
    int slot = alloc_slot();
    if (slot < 0) return 0;

    if (memcmp(bytes, "MThd", 4) == 0) {
        // Already MIDI — load straight from memory.
        // Estimate size by walking MIDI tracks would be complex; use a
        // large upper bound from the GLB (the data buffer is at least
        // big enough to hold the MIDI). We don't know the size, so we
        // assume a generous 256 KB max — Mix_LoadMUS_RW reads only what
        // it needs from the SDL_RWops cursor.
        SDL_RWops *rw = SDL_RWFromConstMem(data, 256 * 1024);
        Mix_Music *m = Mix_LoadMUS_RW(rw, 1);
        if (!m) {
            fprintf(stderr, "[audio] Mix_LoadMUS_RW(MIDI): %s\n", Mix_GetError());
            return 0;
        }
        g_slots[slot].music    = m;
        g_slots[slot].in_use   = 1;
        return slot;
    }

    if (memcmp(bytes, "MUS\x1A", 4) == 0) {
        unsigned short score_len   = (unsigned short)bytes[4] | ((unsigned short)bytes[5] << 8);
        unsigned short score_start = (unsigned short)bytes[6] | ((unsigned short)bytes[7] << 8);
        size_t mus_bytes = (size_t)score_start + (size_t)score_len;

        // mus2mid takes FILE *. Use temp files in /tmp; simpler than
        // wrestling fmemopen + open_memstream cross-platform quirks.
        char mus_path[64], mid_path[64];
        snprintf(mus_path, sizeof mus_path, "/tmp/raptor_mus_%d.mus", slot);
        snprintf(mid_path, sizeof mid_path, "/tmp/raptor_mus_%d.mid", slot);

        FILE *fm = fopen(mus_path, "wb");
        if (!fm) return 0;
        fwrite(data, 1, mus_bytes, fm);
        fclose(fm);

        fm = fopen(mus_path, "rb");
        FILE *fmid = fopen(mid_path, "wb");
        if (!fm || !fmid) {
            if (fm)   fclose(fm);
            if (fmid) fclose(fmid);
            unlink(mus_path);
            return 0;
        }
        bool rc = mus2mid(fm, fmid, 89 /* default tempo */, 0);
        fclose(fm);
        fclose(fmid);
        unlink(mus_path);
        if (rc) {
            fprintf(stderr, "[audio] mus2mid failed on song slot %d\n", slot);
            unlink(mid_path);
            return 0;
        }

        Mix_Music *m = Mix_LoadMUS(mid_path);
        if (!m) {
            fprintf(stderr, "[audio] Mix_LoadMUS(%s): %s\n", mid_path, Mix_GetError());
            unlink(mid_path);
            return 0;
        }
        g_slots[slot].music = m;
        g_slots[slot].in_use = 1;
        snprintf(g_slots[slot].midi_path, sizeof g_slots[slot].midi_path, "%s", mid_path);
        return slot;
    }

    // Unknown header — try as MIDI anyway (some assets are pre-converted).
    SDL_RWops *rw = SDL_RWFromConstMem(data, 256 * 1024);
    Mix_Music *m = Mix_LoadMUS_RW(rw, 1);
    if (m) {
        g_slots[slot].music = m;
        g_slots[slot].in_use = 1;
        return slot;
    }
    return 0;
}

void MUS_UnregisterSong(int handle) { free_slot(handle); }

/* Debug knob: silence music without ripping out the audio init chain.
 * INTRO_Credits still polls SND_IsSongPlaying so we mute via volume
 * rather than skipping playback (skipping makes the intro hang forever
 * waiting for a song that never ends). */
static int music_muted(void) { return getenv("RAPTOR_NO_MUSIC") != NULL; }

void MUS_PlaySong(int handle, int volume)
{
    if (handle <= 0 || handle >= MUS_MAX_SLOTS) return;
    if (!g_slots[handle].in_use) return;
    if (music_muted()) volume = 0;
    Mix_VolumeMusic(volume);
    /* Play once. The original DMX MUS_PlaySong is one-shot; the looping
     * variant is MUS_ChainSong. Without this distinction, INTRO_Credits
     * hangs in `while (SND_IsSongPlaying())` because the Apogee theme
     * never ends and the Apogee logo stays on screen forever. */
    Mix_PlayMusic(g_slots[handle].music, 0);
    g_current_slot = handle;
}

void MUS_StopSong(int handle)
{
    (void)handle;
    Mix_HaltMusic();
    g_current_slot = -1;
}

void MUS_ChainSong(int handle, int next)
{
    // SDL_mixer doesn't expose chain-on-finish; treat as plain loop.
    (void)next;
    if (handle > 0 && handle < MUS_MAX_SLOTS && g_slots[handle].in_use) {
        if (music_muted()) Mix_VolumeMusic(0);
        Mix_PlayMusic(g_slots[handle].music, -1);
    }
}

void MUS_FadeInSong(int handle, int ms)
{
    if (handle <= 0 || handle >= MUS_MAX_SLOTS) return;
    if (!g_slots[handle].in_use) return;
    if (music_muted()) Mix_VolumeMusic(0);
    Mix_FadeInMusic(g_slots[handle].music, -1, ms);
    g_current_slot = handle;
}

void MUS_FadeOutSong(int handle, int ms)
{
    (void)handle;
    Mix_FadeOutMusic(ms);
}

int MUS_QrySongPlaying(int handle)
{
    (void)handle;
    return Mix_PlayingMusic() ? 1 : 0;
}

void MUS_SetMasterVolume(int volume) { Mix_VolumeMusic(volume); }

// ---- SFX (DMX patches) -------------------------------------------------
//
// DMX patch layout (8-bit unsigned PCM):
//   0..1   type (uint16 LE; 3 = digitized)
//   2..3   sample rate (Hz, uint16 LE)
//   4..7   total length (uint32 LE) — includes 32 bytes of pad
//   8..23  padding
//   24..   8-bit unsigned PCM, length = total - 32

typedef struct {
    int        in_use;
    Mix_Chunk *chunk;
    Uint8     *resampled;
} sfx_play_t;

#define SFX_MAX 16
static sfx_play_t g_sfx[SFX_MAX];

// Upsample 8-bit unsigned mono at rate `src_rate` to 16-bit signed
// stereo at 44100 Hz. Linear nearest-neighbor — fine for game blips.
static Mix_Chunk *make_chunk_from_dmx_pcm(const unsigned char *pcm,
                                          unsigned int  src_rate,
                                          unsigned long src_samples)
{
    if (src_rate == 0 || src_samples == 0) return NULL;
    const int dst_rate = 44100;
    Uint64 dst_samples = (Uint64)src_samples * dst_rate / src_rate;
    size_t dst_bytes = (size_t)dst_samples * 2 /* channels */ * sizeof(Sint16);
    Sint16 *out = (Sint16 *)malloc(dst_bytes);
    if (!out) return NULL;

    for (Uint64 i = 0; i < dst_samples; i++) {
        Uint64 src_idx = i * src_rate / dst_rate;
        if (src_idx >= src_samples) src_idx = src_samples - 1;
        // 8-bit unsigned -> 16-bit signed: ((u - 128) << 8)
        Sint16 s = (Sint16)((int)pcm[src_idx] - 128) << 8;
        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
    }

    Mix_Chunk *c = Mix_QuickLoad_RAW((Uint8 *)out, (Uint32)dst_bytes);
    if (!c) { free(out); return NULL; }
    return c;
}

int SFX_PlayPatch(void *vdata, int pitch, int sep, int vol,
                  int unused, int priority)
{
    (void)pitch; (void)priority; (void)unused;
    if (!g_audio_ready || !vdata) return -1;

    const unsigned char *data = (const unsigned char *)vdata;
    unsigned short type = (unsigned short)(data[0] | ((unsigned short)data[1] << 8));
    if (type != 3) return -1;   // PC speaker not supported

    unsigned int   rate = (unsigned int)(data[2] | (data[3] << 8));
    unsigned long  total_len = ((unsigned long)data[4]) |
                               ((unsigned long)data[5] << 8) |
                               ((unsigned long)data[6] << 16) |
                               ((unsigned long)data[7] << 24);
    if (total_len <= 32) return -1;
    unsigned long sample_len = total_len - 32;

    Mix_Chunk *c = make_chunk_from_dmx_pcm(data + 24, rate, sample_len);
    if (!c) return -1;

    int channel = Mix_PlayChannel(-1, c, 0);
    if (channel < 0) {
        free(c->abuf);
        Mix_FreeChunk(c);
        return -1;
    }

    // Volume + pan. apodmx sep range is 0..254 around 127 center. Raw vol
    // is 0..127. SDL volume is 0..MIX_MAX_VOLUME (128).
    Mix_Volume(channel, (vol * MIX_MAX_VOLUME) / 127);
    Uint8 right = (Uint8)((sep < 254 ? sep : 254));
    Uint8 left  = (Uint8)(254 - right);
    Mix_SetPanning(channel, left, right);

    // Track resampled buffer + chunk so we can free on stop. Channel
    // index doubles as the SFX_HANDLE.
    if (channel >= 0 && channel < SFX_MAX) {
        if (g_sfx[channel].in_use) {
            // previous chunk on this channel — free it
            if (g_sfx[channel].chunk)     Mix_FreeChunk(g_sfx[channel].chunk);
            if (g_sfx[channel].resampled) free(g_sfx[channel].resampled);
        }
        g_sfx[channel].chunk     = c;
        g_sfx[channel].resampled = c->abuf;
        g_sfx[channel].in_use    = 1;
    }
    return channel;
}

void SFX_StopPatch(int handle)
{
    if (handle < 0 || handle >= SFX_MAX) return;
    Mix_HaltChannel(handle);
    if (g_sfx[handle].in_use) {
        if (g_sfx[handle].chunk)     Mix_FreeChunk(g_sfx[handle].chunk);
        if (g_sfx[handle].resampled) free(g_sfx[handle].resampled);
        memset(&g_sfx[handle], 0, sizeof g_sfx[handle]);
    }
}

int SFX_Playing(int handle)
{
    if (handle < 0 || handle >= SFX_MAX) return 0;
    return Mix_Playing(handle) ? 1 : 0;
}

// ---- audiolib sound-card detection ------------------------------------
//
// FX.C calls these from SND_InitSound, with the signatures shown below
// (audiolib AL_109 conventions; not what the apodmx headers declared).
// Returning 0 = "card present" puts the game on its happy audio path;
// the actual playback all goes through DMX_Init -> SDL2_mixer above.
//
//   AL_Detect ( int *iobase, int *cardtype )    /* AdLib  */
//   AL_SetCard ( int cardtype, void *patches )
//   GF1_Detect ( void )                          /* GUS    */
//   GF1_SetMap ( void *map, int size )
//   MPU_Detect ( int *port, int *irq )           /* MPU401 */
//   MPU_SetCard ( int port )
//   MV_Detect  ( void )                          /* MultiVoc / PAS */
//   SB_Detect  ( int *port, int *irq, int *dma, int *cardtype )  /* Sound Blaster */
//   SB_SetCard ( int port, int irq, int dma )
//   WAV_PlayMode ( int rate, int channels )
int  AL_Detect    (int *iobase, int *cardtype) {
    if (iobase)   *iobase   = 0x388;
    if (cardtype) *cardtype = 1;
    return 0;
}
void AL_SetCard   (int cardtype, void *patches) { (void)cardtype; (void)patches; }

int  GF1_Detect   (void) { return -1; }
void GF1_SetMap   (void *map, int size) { (void)map; (void)size; }

int  MPU_Detect   (int *port, int *irq) {
    if (port) *port = 0x330;
    if (irq)  *irq  = 9;
    return 0;
}
void MPU_SetCard  (int port) { (void)port; }

int  MV_Detect    (void) { return -1; }

int  SB_Detect    (int *port, int *irq, int *dma, int *cardtype) {
    if (port)     *port     = 0x220;
    if (irq)      *irq      = 7;
    if (dma)      *dma      = 1;
    if (cardtype) *cardtype = 5;
    return 0;
}
void SB_SetCard   (int port, int irq, int dma) { (void)port; (void)irq; (void)dma; }

int  WAV_PlayMode (int rate, int chans) { (void)rate; (void)chans; return 0; }
