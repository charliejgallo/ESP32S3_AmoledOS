/*
 * AmoledOS - audio files in, PCM out. See aos_audio.h.
 */
#include "aos_audio.h"
#include "aos_aac.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
/* Everything big goes to PSRAM, and with no PSRAM left it fails rather than
 * quietly eating the internal RAM that WiFi and the SD's DMA live on. */
static void *big_alloc(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}
static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}
#else
#include <time.h>
static void *big_alloc(size_t n)
{
    return calloc(1, n);
}
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
#endif

/* minimp3 twice: the first include declares mp3dec_t, so the decoder's state
 * can carry the scratch right behind it; the second compiles the decoder
 * with MINIMP3_SCRATCH pointing there. 'dec' is the name of the state inside
 * mp3dec_decode_frame(). Layers I and II are left out (MINIMP3_ONLY_MP3). */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3/minimp3.h"

typedef struct {
    mp3dec_t dec;               /* first: the cast below depends on it */
    void    *scratch;
} mp3_state_t;

#define MINIMP3_SCRATCH ((mp3dec_scratch_t *)((mp3_state_t *)dec)->scratch)
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

#define IN_SIZE         (16 * 1024)     /* file bytes in hand: 400 ms at 320 kbps */
#define IN_LOW          (4 * 1024)      /* refill under this: two frames and change */

struct aos_audio {
    FILE    *file;
    aos_audio_src_fn src;       /* instead of the file: a stream */
    void    *src_ctx;
    aos_audio_format_t format;
    uint8_t  channels;
    uint32_t sample_rate;
    uint32_t data_start;        /* first byte of audio (after the ID3 tag / WAV header) */
    uint32_t data_end;          /* one past the last (before an ID3v1 tag) */
    uint32_t file_pos;          /* where the next fread starts */

    /* WAV */
    uint16_t block_align;
    uint32_t byte_rate;

    /* MP3 */
    mp3_state_t mp3;
    uint16_t kbps;
    uint32_t duration_ms;
    bool     has_toc;
    bool     vbr;
    uint8_t  toc[100];
    uint8_t *in;                /* IN_SIZE, PSRAM */
    int      in_len, in_pos;
    bool     in_eof;
    int16_t *pcm;               /* one decoded frame, MINIMP3_MAX_SAMPLES_PER_FRAME */
    int      pcm_frames, pcm_pos, pcm_channels;

    /* Gapless: what the encoder added. An encoder delays the audio (LAME:
     * 576 samples, plus the 529 every layer III decoder adds) and pads the
     * last frame; the LAME tag in the Info frame says how much, and the Info
     * frame itself decodes to one frame of silence. Without trimming them,
     * a track that runs into the next one gets a click and ~50 ms of
     * silence between them, which is what ffmpeg removes and we did not. */
    uint32_t skip;              /* output frames still to drop at the start */
    uint64_t out_pos;           /* output frames given, counted after the skip */
    uint64_t out_end;           /* frames in the track as encoded, 0 = unknown */
    uint32_t xing_frames;
    int32_t  enc_delay, enc_padding;   /* -1: no LAME tag */

    uint32_t cost_frames;
    uint64_t cost_us;

    /* AAC (streams only): the decoder, and what the ADTS headers say */
    aos_aac_t *aac;
    uint32_t core_rate;         /* the header's: half the output with SBR */
    uint8_t  core_channels;
};

/* ---- small helpers ------------------------------------------------------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t syncsafe32(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

static bool read_at(FILE *f, uint32_t pos, void *buf, size_t len)
{
    return fseek(f, (long)pos, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static void utf8_put(char **o, char *end, uint32_t cp)
{
    char *p = *o;
    if (cp < 0x80) {
        if (p + 1 >= end) return;
        *p++ = (char)cp;
    } else if (cp < 0x800) {
        if (p + 2 >= end) return;
        *p++ = (char)(0xC0 | (cp >> 6));
        *p++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        if (p + 3 >= end) return;
        *p++ = (char)(0xE0 | (cp >> 12));
        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *p++ = (char)(0x80 | (cp & 0x3F));
    } else {
        if (p + 4 >= end) return;
        *p++ = (char)(0xF0 | (cp >> 18));
        *p++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *p++ = (char)(0x80 | (cp & 0x3F));
    }
    *o = p;
}

/* An ID3 text in any of its four encodings, to UTF-8. Stops at the first
 * terminator: a v2.4 frame may hold several values separated by one, and the
 * first is the one worth showing. */
static void id3_text(const uint8_t *s, int len, char *out, size_t out_len)
{
    char *o = out, *end = out + out_len;
    if (len < 1 || out_len == 0) {
        if (out_len) *out = '\0';
        return;
    }
    uint8_t enc = s[0];
    s++; len--;
    if (enc == 0 || enc == 3) {                 /* Latin-1 or UTF-8 */
        for (int i = 0; i < len && s[i]; i++) {
            if (enc == 3) {
                if (o + 1 < end) *o++ = (char)s[i];
            } else {
                utf8_put(&o, end, s[i]);
            }
        }
    } else {                                    /* UTF-16, with BOM (1) or BE (2) */
        bool le = false;
        int i = 0;
        if (enc == 1 && len >= 2) {
            if (s[0] == 0xFF && s[1] == 0xFE) { le = true; i = 2; }
            else if (s[0] == 0xFE && s[1] == 0xFF) { i = 2; }
        }
        for (; i + 1 < len; i += 2) {
            uint32_t u = le ? (uint32_t)(s[i] | (s[i + 1] << 8)) : (uint32_t)((s[i] << 8) | s[i + 1]);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < len) {
                uint32_t lo = le ? (uint32_t)(s[i + 2] | (s[i + 3] << 8)) : (uint32_t)((s[i + 2] << 8) | s[i + 3]);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            utf8_put(&o, end, u);
        }
    }
    *o = '\0';
    /* trailing spaces (ID3v1 habits carried over) */
    while (o > out && o[-1] == ' ') *--o = '\0';
}

/* Where the picture's bytes start inside an APIC (v2.3/2.4) or PIC (v2.2)
 * frame whose first bytes are 'h'. -1 if the frame is cut before that. */
static int id3_picture_start(const uint8_t *h, int len, bool v22, char *mime, size_t mime_len)
{
    if (len < 4) return -1;
    uint8_t enc = h[0];
    int i = 1;
    if (v22) {
        snprintf(mime, mime_len, "image/%.3s", (const char *)h + 1);
        for (char *c = mime; *c; c++) *c = (char)tolower((unsigned char)*c);
        i = 4;
    } else {
        int m = 0;
        while (i < len && h[i]) {
            if (m + 1 < (int)mime_len) mime[m++] = (char)h[i];
            i++;
        }
        mime[m] = '\0';
        i++;                                    /* the terminator */
    }
    i++;                                        /* picture type */
    if (enc == 1 || enc == 2) {                 /* description, 16-bit terminator */
        while (i + 1 < len && (h[i] || h[i + 1])) i += 2;
        i += 2;
    } else {
        while (i < len && h[i]) i++;
        i++;
    }
    return i <= len ? i : -1;
}

/* Reads the ID3v2 tag at the start of the file, if there is one. Returns the
 * tag's total size (where the audio starts). */
static uint32_t id3v2_read(FILE *f, aos_audio_info_t *info)
{
    uint8_t h[10];
    if (!read_at(f, 0, h, 10) || memcmp(h, "ID3", 3) != 0) {
        return 0;
    }
    uint8_t ver = h[3], flags = h[5];
    uint32_t size = syncsafe32(h + 6);
    uint32_t total = 10 + size + ((flags & 0x10) ? 10 : 0);
    if (ver < 2 || ver > 4) {
        return total;                           /* skip what we cannot read */
    }

    uint32_t pos = 10, end = 10 + size;
    if (flags & 0x40) {                         /* extended header */
        uint8_t e[4];
        if (!read_at(f, pos, e, 4)) return total;
        pos += (ver == 4) ? syncsafe32(e) : be32(e) + 4;
    }

    bool v22 = ver == 2;
    int head = v22 ? 6 : 10;
    uint8_t buf[256];
    while (pos + head <= end) {
        uint8_t fh[10];
        if (!read_at(f, pos, fh, head) || fh[0] == 0) {
            break;                              /* padding */
        }
        uint32_t fsize = v22 ? ((uint32_t)fh[3] << 16 | fh[4] << 8 | fh[5])
                       : (ver == 4 ? syncsafe32(fh + 4) : be32(fh + 4));
        uint32_t body = pos + head;
        if (fsize == 0 || body + fsize > end) {
            break;
        }
        char id[5] = {0};
        memcpy(id, fh, v22 ? 3 : 4);

        char *dst = NULL;
        size_t dst_len = 0;
        if (!strcmp(id, "TIT2") || !strcmp(id, "TT2")) { dst = info->title;  dst_len = sizeof(info->title); }
        if (!strcmp(id, "TPE1") || !strcmp(id, "TP1")) { dst = info->artist; dst_len = sizeof(info->artist); }
        if (!strcmp(id, "TALB") || !strcmp(id, "TAL")) { dst = info->album;  dst_len = sizeof(info->album); }

        if (dst && !dst[0]) {
            int n = fsize < sizeof(buf) ? (int)fsize : (int)sizeof(buf);
            if (read_at(f, body, buf, n)) {
                id3_text(buf, n, dst, dst_len);
            }
        } else if ((!strcmp(id, "APIC") || !strcmp(id, "PIC")) && !info->cover_offset) {
            int n = fsize < sizeof(buf) ? (int)fsize : (int)sizeof(buf);
            if (read_at(f, body, buf, n)) {
                int start = id3_picture_start(buf, n, v22, info->cover_mime, sizeof(info->cover_mime));
                if (start > 0 && (uint32_t)start < fsize) {
                    info->cover_offset = body + start;
                    info->cover_size   = fsize - start;
                }
            }
        }
        pos = body + fsize;
    }
    return total;
}

/* ---- WAV ---------------------------------------------------------------- */

static bool wav_open(aos_audio_t *a, aos_audio_info_t *info, uint32_t file_size)
{
    uint8_t riff[12];
    if (!read_at(a->file, 0, riff, 12) || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) {
        return false;
    }
    uint32_t pos = 12;
    bool have_fmt = false;
    uint16_t format = 0, bits = 0;
    uint8_t ch[8];
    while (read_at(a->file, pos, ch, 8)) {
        uint32_t size = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (size < 16 || !read_at(a->file, pos + 8, fmt, 16)) return false;
            format         = fmt[0] | fmt[1] << 8;
            a->channels    = fmt[2];
            a->sample_rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | (uint32_t)fmt[7] << 24;
            a->byte_rate   = fmt[8] | fmt[9] << 8 | fmt[10] << 16 | (uint32_t)fmt[11] << 24;
            a->block_align = fmt[12] | fmt[13] << 8;
            bits           = fmt[14] | fmt[15] << 8;
            have_fmt = true;
        } else if (!memcmp(ch, "data", 4)) {
            if (!have_fmt || format != 1 || bits != 16 || a->channels < 1 || a->channels > 2) {
                return false;
            }
            a->data_start = pos + 8;
            /* a streamed WAV says 0 or 0xFFFFFFFF: trust the file instead */
            a->data_end = (size && a->data_start + size <= file_size) ? a->data_start + size : file_size;
            a->file_pos = a->data_start;
            info->kbps = (uint16_t)(a->byte_rate * 8 / 1000);
            info->duration_ms = a->byte_rate
                ? (uint32_t)((uint64_t)(a->data_end - a->data_start) * 1000 / a->byte_rate) : 0;
            return fseek(a->file, (long)a->data_start, SEEK_SET) == 0;
        }
        pos += 8 + size + (size & 1);
    }
    return false;
}

static int wav_read(aos_audio_t *a, int16_t *pcm, int max_frames)
{
    uint32_t left = (a->data_end - a->file_pos) / a->block_align;
    if ((uint32_t)max_frames > left) max_frames = (int)left;
    if (max_frames <= 0) return 0;
    size_t got = fread(pcm, a->block_align, (size_t)max_frames, a->file);
    if (got == 0) return ferror(a->file) ? -1 : 0;
    a->file_pos += (uint32_t)got * a->block_align;
    return (int)got;
}

/* ---- MP3 ---------------------------------------------------------------- */

/* Returns how many bytes it added. */
static int mp3_refill(aos_audio_t *a)
{
    int keep = a->in_len - a->in_pos;
    if (keep > 0 && a->in_pos > 0) {
        memmove(a->in, a->in + a->in_pos, (size_t)keep);
    }
    a->in_len = keep;
    a->in_pos = 0;
    if (a->src) {
        int got = a->in_eof ? -1 : a->src(a->src_ctx, a->in + a->in_len, IN_SIZE - a->in_len);
        if (got < 0) {
            a->in_eof = true;
            return 0;
        }
        a->in_len += got;
        a->file_pos += (uint32_t)got;
        return got;
    }
    uint32_t want = (uint32_t)(IN_SIZE - a->in_len);
    if (want > a->data_end - a->file_pos) want = a->data_end - a->file_pos;
    if (want == 0) {
        a->in_eof = true;
        return 0;
    }
    size_t got = fread(a->in + a->in_len, 1, want, a->file);
    a->in_len += (int)got;
    a->file_pos += (uint32_t)got;
    if (got < want) a->in_eof = true;
    return (int)got;
}

/* The Xing/Info header of the first frame: frame count (so the length of a
 * VBR file) and the seek table. 'fr' points at the frame's 4-byte header. */
static void mp3_xing(aos_audio_t *a, const uint8_t *fr, int len, const mp3dec_frame_info_t *fi)
{
    bool mpeg1 = (fr[1] & 0x08) != 0;
    int side = mpeg1 ? (fi->channels == 2 ? 32 : 17) : (fi->channels == 2 ? 17 : 9);
    int x = 4 + side;
    if (x + 8 > len) return;
    if (memcmp(fr + x, "Xing", 4) && memcmp(fr + x, "Info", 4)) return;
    a->vbr = memcmp(fr + x, "Xing", 4) == 0;   /* LAME writes "Info" for CBR */
    uint32_t flags = be32(fr + x + 4);
    int p = x + 8;
    uint32_t frames = 0;
    if (flags & 1) {
        if (p + 4 > len) return;
        frames = be32(fr + p);
        p += 4;
    }
    if (flags & 2) p += 4;                      /* byte count: the file tells us */
    if ((flags & 4) && p + 100 <= len) {
        memcpy(a->toc, fr + p, 100);
        a->has_toc = true;
    }
    if (flags & 4) p += 100;
    if (flags & 8) p += 4;                      /* quality */
    /* The LAME tag follows: 9 bytes of encoder name ("LAME3.99r", or "Lavc"
     * from ffmpeg, same layout), and at +21 the delay and the padding, 12
     * bits each. */
    if (p + 24 <= len && (!memcmp(fr + p, "LAME", 4) || !memcmp(fr + p, "Lavc", 4) ||
                          !memcmp(fr + p, "Lavf", 4))) {
        const uint8_t *d = fr + p + 21;
        a->enc_delay   = (d[0] << 4) | (d[1] >> 4);
        a->enc_padding = ((d[1] & 0x0F) << 8) | d[2];
    }
    a->xing_frames = frames;
    uint32_t spf = mpeg1 ? 1152 : 576;
    if (frames && a->sample_rate) {
        a->duration_ms = (uint32_t)((uint64_t)frames * spf * 1000 / a->sample_rate);
    }
}

static bool mp3_open(aos_audio_t *a, aos_audio_info_t *info, uint32_t file_size)
{
    a->data_start = id3v2_read(a->file, info);
    a->data_end = file_size;
    if (file_size >= 128) {
        uint8_t tag[3];
        if (read_at(a->file, file_size - 128, tag, 3) && !memcmp(tag, "TAG", 3)) {
            a->data_end = file_size - 128;
        }
    }
    if (a->data_start >= a->data_end) return false;

    a->in  = big_alloc(IN_SIZE);
    a->pcm = big_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    a->mp3.scratch = big_alloc(sizeof(mp3dec_scratch_t));
    if (!a->in || !a->pcm || !a->mp3.scratch) return false;
    mp3dec_init(&a->mp3.dec);

    a->file_pos = a->data_start;
    if (fseek(a->file, (long)a->file_pos, SEEK_SET) != 0) return false;
    mp3_refill(a);

    /* The first frame, parsed and not decoded (pcm NULL): rate, channels,
     * bitrate. minimp3 wants several frames in a row before it believes a
     * sync word, so garbage between the tag and the audio is skipped here. */
    mp3dec_frame_info_t fi;
    for (int tries = 0; tries < 64; tries++) {
        memset(&fi, 0, sizeof fi);
        int samples = mp3dec_decode_frame(&a->mp3.dec, a->in + a->in_pos,
                                          a->in_len - a->in_pos, NULL, &fi);
        if (samples > 0) break;
        if (fi.frame_bytes > 0) {
            a->in_pos += fi.frame_bytes;
        } else if (a->in_eof) {
            return false;
        } else if (a->in_pos == 0 && a->in_len == IN_SIZE) {
            a->in_pos = a->in_len;              /* 16 KB and no frame: not audio */
        }
        mp3_refill(a);
    }
    if (fi.hz <= 0 || fi.channels <= 0) return false;

    a->sample_rate = (uint32_t)fi.hz;
    a->channels = (uint8_t)fi.channels;
    a->kbps = (uint16_t)fi.bitrate_kbps;
    const uint8_t *fr = a->in + a->in_pos + fi.frame_offset;
    mp3_xing(a, fr, a->in_len - a->in_pos - fi.frame_offset, &fi);
    if (!a->duration_ms && a->kbps) {           /* CBR: bytes over bitrate */
        a->duration_ms = (uint32_t)((uint64_t)(a->data_end - a->data_start) * 8 / a->kbps);
    }
    /* The Info frame decodes to silence; with the LAME tag, the delay goes
     * too, and the padding at the end. Checked against ffmpeg, which does
     * the same: sample for sample, the same start and the same length. */
    uint32_t spf = (fr[1] & 0x08) ? 1152 : 576;
    if (a->xing_frames) {
        a->skip = spf;
        if (a->enc_delay >= 0) {
            a->skip += (uint32_t)a->enc_delay + 529;
            uint64_t total = (uint64_t)a->xing_frames * spf;
            uint32_t cut = (uint32_t)a->enc_delay + (uint32_t)a->enc_padding;
            a->out_end = total > cut ? total - cut : 0;
            a->duration_ms = (uint32_t)(a->out_end * 1000 / a->sample_rate);
        }
    }
    info->vbr = a->vbr;
    info->kbps = a->kbps;
    info->duration_ms = a->duration_ms;
    mp3dec_init(&a->mp3.dec);                   /* decode from this frame, fresh */
    return true;
}

static int mp3_read(aos_audio_t *a, int16_t *pcm, int max_frames)
{
    int done = 0;
    while (done < max_frames) {
        if (a->pcm_pos < a->pcm_frames && a->skip) {
            int drop = a->pcm_frames - a->pcm_pos;
            if ((uint32_t)drop > a->skip) drop = (int)a->skip;
            a->pcm_pos += drop;
            a->skip -= (uint32_t)drop;
            continue;
        }
        if (a->out_end && a->out_pos >= a->out_end) {
            break;                              /* the encoder's padding */
        }
        if (a->pcm_pos < a->pcm_frames) {
            int n = a->pcm_frames - a->pcm_pos;
            if (n > max_frames - done) n = max_frames - done;
            if (a->out_end && (uint64_t)n > a->out_end - a->out_pos) {
                n = (int)(a->out_end - a->out_pos);
            }
            const int16_t *src = a->pcm + a->pcm_pos * a->pcm_channels;
            int16_t *dst = pcm + done * a->channels;
            if (a->pcm_channels == a->channels) {
                memcpy(dst, src, (size_t)n * a->channels * sizeof(int16_t));
            } else if (a->channels == 2) {      /* a mono frame in a stereo file */
                for (int i = 0; i < n; i++) dst[2 * i] = dst[2 * i + 1] = src[i];
            } else {                            /* and the other way round */
                for (int i = 0; i < n; i++) dst[i] = (int16_t)((src[2 * i] + src[2 * i + 1]) / 2);
            }
            a->pcm_pos += n;
            a->out_pos += (uint64_t)n;
            done += n;
            continue;
        }

        if (a->in_len - a->in_pos < IN_LOW && !a->in_eof) {
            mp3_refill(a);
        }
        if (a->in_pos >= a->in_len) {
            break;                              /* end of the file */
        }
        mp3dec_frame_info_t fi;
        memset(&fi, 0, sizeof fi);
        uint64_t t0 = now_us();
        int samples = mp3dec_decode_frame(&a->mp3.dec, a->in + a->in_pos,
                                          a->in_len - a->in_pos, a->pcm, &fi);
        a->cost_us += now_us() - t0;
        if (samples > 0) {
            a->cost_frames += (uint32_t)samples;
            a->in_pos += fi.frame_bytes;
            a->pcm_frames = samples;
            a->pcm_pos = 0;
            a->pcm_channels = fi.channels;
        } else if (fi.frame_bytes > 0) {
            a->in_pos += fi.frame_bytes;        /* junk skipped, or a frame it could not use */
        } else if (a->in_eof) {
            a->in_pos = a->in_len;              /* a torn last frame */
        } else if (a->in_pos == 0 && a->in_len == IN_SIZE) {
            a->in_pos = a->in_len;              /* 16 KB of nothing */
        } else if (mp3_refill(a) == 0 && a->src) {
            break;                              /* a stream, dry for now: a torn frame waits */
        }
    }
    return done;
}

/* ---- the public face ------------------------------------------------------ */

static bool aac_open_src(aos_audio_t *a, aos_audio_info_t *info);
static int  aac_read(aos_audio_t *a, int16_t *pcm, int max_frames);

bool aos_audio_is_playable(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    return dot && (strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".mp3") == 0);
}

aos_audio_t *aos_audio_open(const char *path, aos_audio_info_t *info)
{
    aos_audio_info_t scratch_info;
    if (!info) info = &scratch_info;
    memset(info, 0, sizeof *info);
    if (!path) return NULL;

    aos_audio_t *a = big_alloc(sizeof *a);
    if (!a) return NULL;
    a->enc_delay = a->enc_padding = -1;
    a->file = fopen(path, "rb");
    struct stat st;
    if (!a->file || stat(path, &st) != 0) {
        aos_audio_close(a);
        return NULL;
    }
    uint32_t size = (uint32_t)st.st_size;

    const char *dot = strrchr(path, '.');
    bool ok;
    if (dot && strcasecmp(dot, ".mp3") == 0) {
        a->format = AOS_AUDIO_MP3;
        ok = mp3_open(a, info, size);
    } else {
        a->format = AOS_AUDIO_WAV;
        ok = wav_open(a, info, size);
    }
    info->format = a->format;
    info->sample_rate = a->sample_rate;
    info->channels = a->channels;
    if (!ok) {
        aos_audio_close(a);
        return NULL;
    }
    return a;
}

aos_audio_t *aos_audio_open_src(aos_audio_src_fn fn, void *ctx, aos_audio_info_t *info)
{
    aos_audio_info_t scratch_info;
    if (!info) info = &scratch_info;
    memset(info, 0, sizeof *info);
    if (!fn) return NULL;

    aos_audio_t *a = big_alloc(sizeof *a);
    if (!a) return NULL;
    a->enc_delay = a->enc_padding = -1;
    a->src = fn;
    a->src_ctx = ctx;
    a->format = AOS_AUDIO_MP3;
    a->data_end = UINT32_MAX;
    a->in  = big_alloc(IN_SIZE);
    a->pcm = big_alloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t));
    a->mp3.scratch = big_alloc(sizeof(mp3dec_scratch_t));
    if (!a->in || !a->pcm || !a->mp3.scratch) {
        aos_audio_close(a);
        return NULL;
    }
    mp3dec_init(&a->mp3.dec);
    mp3_refill(a);
    if (aac_open_src(a, info)) {
        return a;
    }
    if (a->format == AOS_AUDIO_AAC) {       /* ADTS, but it would not decode */
        aos_audio_close(a);
        return NULL;
    }

    /* The first frame, as for a file. The caller waited for a buffer's worth
     * before opening, so 16 KB are in hand; minimp3 wants several frames in
     * a row before it believes a sync word, and a stream joined halfway
     * starts in the middle of one. */
    mp3dec_frame_info_t fi;
    memset(&fi, 0, sizeof fi);
    for (int tries = 0; tries < 64; tries++) {
        memset(&fi, 0, sizeof fi);
        int samples = mp3dec_decode_frame(&a->mp3.dec, a->in + a->in_pos,
                                          a->in_len - a->in_pos, NULL, &fi);
        if (samples > 0) break;
        if (fi.frame_bytes > 0) {
            a->in_pos += fi.frame_bytes;
        } else if (a->in_eof) {
            break;
        } else if (a->in_pos == 0 && a->in_len == IN_SIZE) {
            a->in_pos = a->in_len;              /* 16 KB and no frame: not MP3 */
            break;
        }
        mp3_refill(a);
    }
    if (fi.hz <= 0 || fi.channels <= 0) {
        aos_audio_close(a);
        return NULL;
    }
    a->sample_rate = (uint32_t)fi.hz;
    a->channels = (uint8_t)fi.channels;
    a->kbps = (uint16_t)fi.bitrate_kbps;
    info->format = AOS_AUDIO_MP3;
    info->sample_rate = a->sample_rate;
    info->channels = a->channels;
    info->kbps = a->kbps;
    snprintf(info->codec, sizeof(info->codec), "MP3");
    mp3dec_init(&a->mp3.dec);
    return a;
}

bool aos_audio_ended(const aos_audio_t *a)
{
    return !a || !a->src || (a->in_eof && a->in_pos >= a->in_len && a->pcm_pos >= a->pcm_frames);
}

int aos_audio_read(aos_audio_t *a, int16_t *pcm, int max_frames)
{
    if (!a || !pcm || max_frames <= 0) return -1;
    if (a->format == AOS_AUDIO_AAC) return aac_read(a, pcm, max_frames);
    return a->format == AOS_AUDIO_MP3 ? mp3_read(a, pcm, max_frames)
                                      : wav_read(a, pcm, max_frames);
}

uint32_t aos_audio_seek(aos_audio_t *a, uint32_t ms)
{
    if (!a || a->src) return 0;
    if (a->format == AOS_AUDIO_WAV) {
        uint64_t off = (uint64_t)ms * a->byte_rate / 1000;
        off -= off % a->block_align;
        if (a->data_start + off > a->data_end) off = a->data_end - a->data_start;
        a->file_pos = a->data_start + (uint32_t)off;
        fseek(a->file, (long)a->file_pos, SEEK_SET);
        return a->byte_rate ? (uint32_t)(off * 1000 / a->byte_rate) : 0;
    }

    if (a->duration_ms && ms >= a->duration_ms) ms = a->duration_ms;
    uint32_t span = a->data_end - a->data_start;
    uint64_t off;
    if (a->vbr && a->has_toc && a->duration_ms) {
        /* The Xing table: 100 points, each a fraction (of 256) of the stream.
         * Only for VBR: a 20 MB file has 78 KB per step of 1/256, 2 s at
         * 320 kbps, and a CBR file's "Info" table landed half a second off
         * where plain arithmetic lands on the frame. */
        float pct = (float)ms * 100.0f / (float)a->duration_ms;
        int i = (int)pct;
        if (i > 99) i = 99;
        float lo = a->toc[i], hi = i < 99 ? a->toc[i + 1] : 256.0f;
        off = (uint64_t)((lo + (hi - lo) * (pct - (float)i)) / 256.0f * (float)span);
    } else {
        off = (uint64_t)ms * a->kbps / 8;       /* CBR: kbit/s = bytes per ms * 8 */
    }
    if (off > span) off = span;
    a->file_pos = a->data_start + (uint32_t)off;
    fseek(a->file, (long)a->file_pos, SEEK_SET);
    a->in_len = a->in_pos = 0;
    a->in_eof = false;
    a->pcm_frames = a->pcm_pos = 0;
    a->skip = 0;
    a->out_pos = (uint64_t)ms * a->sample_rate / 1000;
    mp3dec_init(&a->mp3.dec);                   /* it resyncs on its own */
    mp3_refill(a);
    return ms;
}

void aos_audio_cost(const aos_audio_t *a, uint32_t *frames, uint64_t *us)
{
    if (frames) *frames = a ? a->cost_frames : 0;
    if (us) *us = a ? a->cost_us : 0;
}

void aos_audio_close(aos_audio_t *a)
{
    if (!a) return;
    if (a->file) fclose(a->file);
    aos_aac_close(a->aac);
    free(a->in);
    free(a->pcm);
    free(a->mp3.scratch);
    free(a);
}

/* ---- a folder as a playlist ------------------------------------------------ */

/* Names in the order a person expects: case ignored and digit runs compared
 * as numbers, so "2 - x" comes before "10 - y". */
int aos_audio_name_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *ea = a, *eb = b;
            while (isdigit((unsigned char)*ea)) ea++;
            while (isdigit((unsigned char)*eb)) eb++;
            if (ea - a != eb - b) return (int)((ea - a) - (eb - b));
            int c = strncmp(a, b, (size_t)(ea - a));
            if (c) return c;
            a = ea;
            b = eb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

bool aos_audio_list_scan(aos_audio_list_t *list, const char *dir, int max)
{
    aos_audio_list_free(list);
    snprintf(list->dir, sizeof(list->dir), "%s", dir);

    /* Two passes over the folder: count, then copy. Cheaper than growing a
     * PSRAM block and the FAT directory is read from its cache the second
     * time. */
    DIR *d = opendir(dir);
    if (!d) return false;
    int count = 0;
    size_t bytes = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        if (e->d_name[0] != '.' && aos_audio_is_playable(e->d_name)) {
            count++;
            bytes += strlen(e->d_name) + 1;
        }
    }
    closedir(d);
    if (count == 0) return true;

    list->names  = big_alloc(bytes);
    list->offset = big_alloc((size_t)count * sizeof(uint32_t));
    if (!list->names || !list->offset) {
        aos_audio_list_free(list);
        return false;
    }
    d = opendir(dir);
    if (!d) {
        aos_audio_list_free(list);
        return false;
    }
    size_t used = 0;
    while ((e = readdir(d)) != NULL && list->count < count) {
        if (e->d_name[0] == '.' || !aos_audio_is_playable(e->d_name)) continue;
        size_t len = strlen(e->d_name) + 1;
        if (used + len > bytes) break;          /* the folder changed in between */
        memcpy(list->names + used, e->d_name, len);
        list->offset[list->count++] = (uint32_t)used;
        used += len;
    }
    closedir(d);

    /* insertion sort: a folder is tens of files, and qsort has no context */
    for (int i = 1; i < list->count; i++) {
        uint32_t v = list->offset[i];
        int j = i - 1;
        while (j >= 0 && aos_audio_name_cmp(list->names + list->offset[j], list->names + v) > 0) {
            list->offset[j + 1] = list->offset[j];
            j--;
        }
        list->offset[j + 1] = v;
    }
    return true;
}

void aos_audio_list_free(aos_audio_list_t *list)
{
    free(list->names);
    free(list->offset);
    list->names = NULL;
    list->offset = NULL;
    list->count = 0;
}

const char *aos_audio_list_name(const aos_audio_list_t *list, int index)
{
    if (!list || index < 0 || index >= list->count) return NULL;
    return list->names + list->offset[index];
}

int aos_audio_list_find(const aos_audio_list_t *list, const char *name)
{
    for (int i = 0; list && i < list->count; i++) {
        if (strcmp(list->names + list->offset[i], name) == 0) return i;
    }
    return -1;
}

/* ---- AAC (branch aac) ------------------------------------------------------
 *
 * ADTS frames out of the same input buffer the MP3 path uses: a 7-byte
 * header with a 12-bit sync (0xFFF), layer 00, and the frame's whole length
 * in 13 bits. A frame is handed to the decoder only when all of it is in
 * the buffer; a byte that is not a header is skipped until one is found,
 * which is also how a stream joined halfway lines up. */

static const uint32_t ADTS_RATES[16] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                         22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0 };

/* The frame's length if 'p' is an ADTS header, 0 if there are not 7 bytes
 * to tell, -1 if it is not one. */
static int adts_len(const uint8_t *p, int avail)
{
    if (avail < 7) {
        return 0;
    }
    if (p[0] != 0xFF || (p[1] & 0xF6) != 0xF0 || ADTS_RATES[(p[2] >> 2) & 0x0F] == 0) {
        return -1;
    }
    int len = ((p[3] & 0x03) << 11) | (p[4] << 3) | (p[5] >> 5);
    return len >= 7 ? len : -1;
}

/* A header here and another right where it says it ends. */
static bool adts_confirmed(const uint8_t *p, int avail)
{
    int len = adts_len(p, avail);
    return len > 0 && len + 2 <= avail && adts_len(p + len, avail - len) != -1 &&
           p[len] == 0xFF && (p[len + 1] & 0xF6) == 0xF0;
}

/* Decodes one frame at in_pos into a->pcm. >0 samples, 0 nothing decoded
 * (a bad frame, skipped), -1 not enough bytes yet. */
static int aac_frame(aos_audio_t *a)
{
    int avail = a->in_len - a->in_pos;
    const uint8_t *p = a->in + a->in_pos;
    int len = adts_len(p, avail);
    if (len < 0) {
        a->in_pos++;                    /* not a header: look further */
        return 0;
    }
    if (len == 0 || len > avail) {
        return -1;
    }
    uint32_t rate = 0;
    uint8_t ch = 0;
    uint64_t t0 = now_us();
    int n = aos_aac_decode(a->aac, p, len, a->pcm, &rate, &ch);
    a->cost_us += now_us() - t0;
    if (n > 0) {
        a->cost_frames += (uint32_t)n;
        a->core_rate = ADTS_RATES[(p[2] >> 2) & 0x0F];
        a->core_channels = (uint8_t)(((p[2] & 0x01) << 2) | (p[3] >> 6));
        a->pcm_frames = n;
        a->pcm_pos = 0;
        a->pcm_channels = ch;
        if (!a->sample_rate) {
            a->sample_rate = rate;
            a->channels = ch;
        }
    }
    a->in_pos += len;
    return n > 0 ? n : 0;
}

static int aac_read(aos_audio_t *a, int16_t *pcm, int max_frames)
{
    int done = 0;
    while (done < max_frames) {
        if (a->pcm_pos < a->pcm_frames) {
            int n = a->pcm_frames - a->pcm_pos;
            if (n > max_frames - done) n = max_frames - done;
            const int16_t *src = a->pcm + a->pcm_pos * a->pcm_channels;
            int16_t *dst = pcm + done * a->channels;
            if (a->pcm_channels == a->channels) {
                memcpy(dst, src, (size_t)n * a->channels * sizeof(int16_t));
            } else if (a->channels == 2) {
                for (int i = 0; i < n; i++) dst[2 * i] = dst[2 * i + 1] = src[i];
            } else {
                for (int i = 0; i < n; i++) dst[i] = (int16_t)((src[2 * i] + src[2 * i + 1]) / 2);
            }
            a->pcm_pos += n;
            done += n;
            continue;
        }
        if (a->in_len - a->in_pos < IN_LOW && !a->in_eof) {
            mp3_refill(a);
        }
        if (a->in_pos >= a->in_len) {
            break;
        }
        int r = aac_frame(a);
        if (r < 0) {
            if (a->in_eof) {
                a->in_pos = a->in_len;      /* a torn last frame */
            } else if (mp3_refill(a) == 0) {
                break;                      /* dry for now */
            }
        }
    }
    return done;
}

/* Called from aos_audio_open_src() with the first bytes in 'in': true if
 * they are ADTS, and then the decoder is open and the first frame decoded,
 * so the output rate and channels (twice the header's rate with SBR, two
 * channels out of one with PS) are known before the player opens the
 * codec. */
static bool aac_open_src(aos_audio_t *a, aos_audio_info_t *info)
{
    int at = -1;
    for (int i = 0; i + 8 < a->in_len; i++) {
        if (a->in[i] != 0xFF) {
            continue;
        }
        if (adts_confirmed(a->in + i, a->in_len - i)) {
            at = i;
            break;
        }
        if ((a->in[i + 1] & 0xE0) == 0xE0 && ((a->in[i + 1] >> 1) & 3) != 0) {
            return false;               /* an MPEG audio header first: MP3 */
        }
    }
    if (at < 0) {
        return false;
    }
    a->aac = aos_aac_open();
    free(a->pcm);
    a->pcm = big_alloc(AOS_AAC_MAX_SAMPLES * sizeof(int16_t));
    if (!a->aac || !a->pcm) {
        return false;
    }
    a->format = AOS_AUDIO_AAC;
    a->in_pos = at;
    uint32_t bytes = 0;
    int frames = 0;
    for (int tries = 0; tries < 16 && !a->sample_rate; tries++) {
        int start = a->in_pos;
        int r = aac_frame(a);
        if (r < 0 && (a->in_eof || mp3_refill(a) == 0)) {
            break;
        }
        if (a->in_pos > start) {
            bytes += (uint32_t)(a->in_pos - start);
            frames++;
        }
    }
    if (!a->sample_rate || !a->channels) {
        return false;
    }
    /* the bitrate from the frames' sizes: 1024 core samples each */
    if (frames && a->core_rate) {
        a->kbps = (uint16_t)((uint64_t)bytes * 8 * a->core_rate / (1024u * (uint32_t)frames) / 1000);
    }
    bool sbr = a->sample_rate > a->core_rate;
    bool ps = a->core_channels == 1 && a->channels == 2;
    snprintf(info->codec, sizeof(info->codec), "%s", ps ? "HE-AACv2" : sbr ? "HE-AAC" : "AAC");
    info->format = AOS_AUDIO_AAC;
    info->sample_rate = a->sample_rate;
    info->channels = a->channels;
    info->kbps = a->kbps;
    return true;
}
