/*
 * AmoledOS - the radio's stream code, on the Mac.
 *
 * The same three files as the board (aos_radio.c, aos_http.c, aos_audio.c)
 * against a real station: it connects, follows what the station answers,
 * decodes for a few seconds in real time and says what it found. It is how
 * the stations in the /radio page's first list were checked, and the quick
 * way to know whether a URL will play on the watch before putting it on a
 * key.
 *
 *   cc -O1 -DAOS_SIM -Icomponents/aos_hal/include -Icomponents/aos_hal \
 *      -I/opt/homebrew/opt/mbedtls/include tools/radio_bench/radio_bench.c \
 *      components/aos_hal/aos_radio.c components/aos_hal/aos_http.c \
 *      components/aos_hal/aos_audio.c -L/opt/homebrew/opt/mbedtls/lib \
 *      -lmbedtls -lmbedx509 -lmbedcrypto -o /tmp/radio_bench
 *
 *   /tmp/radio_bench <url> [seconds] [out.wav]
 *
 * With out.wav it keeps what it decoded, to listen to or to compare with
 * ffmpeg's decode of the same stream.
 */
#include "aos_hal.h"
#include "aos_audio.h"
#include "aos_radio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* aos_http.c asks before a TLS handshake; the Mac always knows the time. */
bool aos_hal_time_is_valid(void)
{
    return true;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static const char *state_name(aos_radio_state_t s)
{
    static const char *const n[] = { "off", "connecting", "buffering", "playing",
                                     "retrying", "failed" };
    return n[s];
}

static void wav_header(FILE *f, uint32_t rate, int ch, uint32_t data)
{
    uint8_t h[44];
    uint32_t v;
    uint16_t w;
    memcpy(h, "RIFF", 4);
    v = 36 + data;               memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;                      memcpy(h + 16, &v, 4);
    w = 1;                       memcpy(h + 20, &w, 2);
    w = (uint16_t)ch;            memcpy(h + 22, &w, 2);
    v = rate;                    memcpy(h + 24, &v, 4);
    v = rate * 2 * (uint32_t)ch; memcpy(h + 28, &v, 4);
    w = (uint16_t)(2 * ch);      memcpy(h + 32, &w, 2);
    w = 16;                      memcpy(h + 34, &w, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <url> [seconds] [out.wav]\n", argv[0]);
        return 2;
    }
    int secs = argc > 2 ? atoi(argv[2]) : 15;
    const char *out = argc > 3 ? argv[3] : NULL;

    double t0 = now();
    aos_radio_start(argv[1]);
    int ready;
    while ((ready = aos_radio_ready()) == 0 && now() - t0 < 25) {
        usleep(50000);
    }
    aos_radio_status_t s;
    memset(&s, 0, sizeof(s));
    aos_radio_fill_status(&s);
    printf("ready=%d after %.2f s  state=%s  error='%s'\n", ready, now() - t0,
           state_name(s.state), s.error);
    printf("host=%s tls=%d  name='%s' genre='%s'  icy-br=%u  type=%s  buffered=%u ms\n",
           s.host, s.tls, s.icy_name, s.icy_genre, (unsigned)s.kbps, s.content_type,
           (unsigned)s.buffer_ms);
    if (ready != 1) {
        aos_radio_stop();
        return 1;
    }

    aos_audio_info_t info;
    aos_audio_t *a = aos_audio_open_src(aos_radio_read, NULL, &info);
    if (!a) {
        printf("no MP3 frames\n");
        aos_radio_stop();
        return 1;
    }
    printf("MP3 %u Hz, %u ch, %u kbps\n", (unsigned)info.sample_rate,
           (unsigned)info.channels, (unsigned)info.kbps);

    FILE *f = out ? fopen(out, "wb") : NULL;
    if (f) {
        wav_header(f, info.sample_rate, info.channels, 0);
    }
    int16_t pcm[1152 * 2];
    uint64_t frames = 0;
    uint32_t gen = 0;
    int dry = 0;
    char title[128];
    double t1 = now();
    while (now() - t1 < secs) {
        int n = aos_audio_read(a, pcm, 1152);
        if (n <= 0) {
            if (aos_audio_ended(a)) {
                printf("the stream ended\n");
                break;
            }
            dry++;
            usleep(20000);
            continue;
        }
        frames += (uint64_t)n;
        if (f) {
            fwrite(pcm, 2 * info.channels, (size_t)n, f);
        }
        uint32_t g = aos_radio_title_at_read(title, sizeof(title));
        if (g != gen) {
            gen = g;
            printf("[%5.1f s] title #%u '%s'\n", now() - t1, (unsigned)g, title);
        }
        /* real time: never more than two seconds ahead of the clock */
        while ((double)frames / info.sample_rate > (now() - t1) + 2.0) {
            usleep(10000);
        }
    }
    aos_radio_fill_status(&s);
    printf("decoded %.1f s in %.1f s, %d dry waits, %u bytes, %u reconnects, %s '%s'\n",
           (double)frames / info.sample_rate, now() - t1, dry, (unsigned)s.bytes,
           (unsigned)s.reconnects, state_name(s.state), s.error);
    if (f) {
        wav_header(f, info.sample_rate, info.channels,
                   (uint32_t)(frames * 2 * info.channels));
        fclose(f);
    }
    aos_audio_close(a);
    aos_radio_stop();
    usleep(1200000);            /* the reader lets go within a second */
    return 0;
}
