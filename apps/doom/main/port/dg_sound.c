/*
 * AmoledOS - Doom: sound effects on the watch's speaker.
 *
 * Doom's effects are 8-bit unsigned PCM lumps ("DS" + name), mostly at
 * 11025 Hz, with a DMX header and 16 bytes of padding at each end. This is
 * the whole mixer: eight channels, each a pointer into its lump walked at
 * rate/16000 in 16.16 fixed point, summed into 16-bit mono and pushed into
 * the streaming speaker (aos_hal_spk_*), which the app opened at 16 kHz.
 *
 * It runs in the engine's task, called once a frame by S_UpdateSounds; the
 * speaker's ring is one second long and never blocks, so the mixer only
 * keeps it about 100 ms ahead. Stereo separation is dropped: one speaker.
 *
 * The lumps are cached PU_STATIC: with PU_CACHE the zone could purge a
 * sound while a channel still points into it. Shareware Doom's effects are
 * about 300 KB, and they go when the zone goes.
 *
 * The music is mixed into the same buffer by port/dg_opl.c: Chocolate
 * Doom's OPL player on DOSBox's OPL2 emulator, driven by this sample count.
 */
#pragma GCC optimize("O2")

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "m_misc.h"
#include "deh_str.h"

#include "../doom_port.h"
#include "dg_port.h"
#include "aos_hal.h"

#include <stdint.h>
#include <string.h>

#define RATE        16000
#define AHEAD       1600        /* samples kept queued: 100 ms */
#define CHUNK       512
#define CHANNELS    16          /* Doom asks for 8 (snd_channels); room to spare */

typedef struct {
    const uint8_t *data;        /* NULL = silent */
    uint32_t       pos;         /* 16.16 */
    uint32_t       step;
    uint32_t       end;         /* 16.16 */
    int            vol;         /* 0..127 */
} chan_t;

static chan_t   s_ch[CHANNELS];

/* What the mixer costs, in CPU cycles of core 0 (the S3's cycle counter;
 * nothing finer than a millisecond is lent to the apps). */
static volatile uint32_t s_cyc_music, s_cyc_total;

static inline uint32_t cycles(void)
{
#if defined(__XTENSA__)
    uint32_t c;
    __asm__ volatile("rsr.ccount %0" : "=a"(c));
    return c;
#else
    return 0;
#endif
}

void dp_audio_cycles(uint32_t *music, uint32_t *total)
{
    *music = s_cyc_music;
    *total = s_cyc_total;
}
static bool     s_enabled;      /* the app opened the speaker */
static bool     s_on;           /* the module is initialised */
static bool     s_prefix;

void dp_sound_enable(bool on)
{
    s_enabled = on;
}

static boolean snd_init(boolean use_sfx_prefix)
{
    if (!s_enabled) return false;
    s_prefix = use_sfx_prefix;
    memset(s_ch, 0, sizeof s_ch);
    s_on = true;
    return true;
}

static void snd_shutdown(void)
{
    s_on = false;
    memset(s_ch, 0, sizeof s_ch);
}

void dg_sound_shutdown(void)
{
    snd_shutdown();
}

static int snd_lump(sfxinfo_t *sfx)
{
    char name[16];
    if (sfx->link) sfx = sfx->link;
    if (s_prefix) M_snprintf(name, sizeof name, "ds%s", DEH_String(sfx->name));
    else M_StringCopy(name, DEH_String(sfx->name), sizeof name);
    return W_CheckNumForName(name);
}

static void snd_update_params(int channel, int vol, int sep)
{
    (void)sep;
    if (channel >= 0 && channel < CHANNELS) s_ch[channel].vol = vol;
}

static int snd_start(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    (void)sep;
    if (!s_on || channel < 0 || channel >= CHANNELS || sfx->lumpnum < 0) return -1;
    const uint8_t *lump = sfx->driver_data;
    if (!lump) {
        lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
        sfx->driver_data = (void *)lump;
    }
    int len = W_LumpLength(sfx->lumpnum);
    /* DMX header: format 3, rate, sample count; then 16 pad bytes */
    if (len < 8 + 32 || lump[0] != 3 || lump[1] != 0) return -1;
    uint32_t rate = lump[2] | (lump[3] << 8);
    uint32_t n = lump[4] | (lump[5] << 8) | ((uint32_t)lump[6] << 16) | ((uint32_t)lump[7] << 24);
    if (n > (uint32_t)len - 8) n = (uint32_t)len - 8;
    if (n <= 32 || rate == 0) return -1;
    chan_t *c = &s_ch[channel];
    c->data = lump + 8 + 16;
    c->pos = 0;
    c->end = (n - 32) << 16;
    c->step = (rate << 16) / RATE;
    c->vol = vol;
    return channel;
}

static void snd_stop(int channel)
{
    if (channel >= 0 && channel < CHANNELS) s_ch[channel].data = NULL;
}

static boolean snd_playing(int channel)
{
    return channel >= 0 && channel < CHANNELS && s_ch[channel].data != NULL;
}

static void snd_update(void)
{
    if (!s_on) return;
    uint32_t t0 = cycles();
    int queued = aos_hal_spk_queued();
    int need = AHEAD - queued;
    if (need <= 0) return;
    static int16_t buf[CHUNK];
    while (need > 0) {
        int n = need > CHUNK ? CHUNK : need;
        int32_t acc[CHUNK];
        memset(acc, 0, sizeof(int32_t) * (size_t)n);
        for (int k = 0; k < CHANNELS; k++) {
            chan_t *c = &s_ch[k];
            if (!c->data) continue;
            const uint8_t *d = c->data;
            uint32_t pos = c->pos, step = c->step, end = c->end;
            int vol = c->vol;
            for (int i = 0; i < n; i++) {
                if (pos >= end) {
                    c->data = NULL;
                    break;
                }
                acc[i] += ((int)d[pos >> 16] - 128) * vol;
                pos += step;
            }
            c->pos = pos;
        }
        uint32_t m0 = cycles();
        dg_opl_mix(acc, n);             /* the music, on the same clock */
        s_cyc_music += cycles() - m0;
        for (int i = 0; i < n; i++) {
            int32_t v = acc[i] * 2;
            /* a soft knee over 3/4 of full scale: music and a shotgun
             * together bend instead of clipping flat */
            int32_t a = v < 0 ? -v : v;
            if (a > 24576) {
                a = 24576 + (a - 24576) / 4;
                if (a > 32767) a = 32767;
                v = v < 0 ? -a : a;
            }
            buf[i] = (int16_t)v;
        }
#ifdef AOS_SIM
        /* DOOM_WAV=<file>: the mix as raw 16 kHz mono, to measure it on the
         * Mac without listening (level, clipping, the music's tempo) */
        {
            static FILE *dump;
            static bool tried;
            if (!tried) {
                tried = true;
                const char *path = getenv("DOOM_WAV");
                if (path) dump = fopen(path, "wb");
            }
            if (dump) {
                fwrite(buf, 2, (size_t)n, dump);
                fflush(dump);
            }
        }
#endif
        aos_hal_spk_write(buf, n);
        need -= n;
    }
    s_cyc_total += cycles() - t0;
}

static void snd_cache(sfxinfo_t *sounds, int num)
{
    (void)sounds;
    (void)num;
}

static snddevice_t s_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    s_devices,
    sizeof s_devices / sizeof s_devices[0],
    snd_init,
    snd_shutdown,
    snd_lump,
    snd_update,
    snd_update_params,
    snd_start,
    snd_stop,
    snd_playing,
    snd_cache,
};

/* i_sound.c binds these to the config file; they belonged to the SDL module */
int   use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
