/*
 * Recorder - what lives on the card.
 */
#include "rec_files.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"    /* aos_month_name */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WAV_HEADER_MIN  44

typedef struct __attribute__((packed)) {
    char     id[4];
    uint32_t size;
} chunk_t;

typedef struct __attribute__((packed)) {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
} fmt_t;

/* Walks the WAV's chunks until it finds 'fmt ' and 'data'. Returns where the
 * audio starts and how long it is. A WAV written by us has both chunks right
 * at the beginning, but any other program may put LIST, fact or whatever it
 * likes in between. */
static bool wav_probe(FILE *file, fmt_t *fmt_out, uint32_t *data_off,
                      uint32_t *data_len)
{
    char riff[12];
    if (fread(riff, 1, sizeof(riff), file) != sizeof(riff)) {
        return false;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool have_fmt = false;
    for (int guard = 0; guard < 16; guard++) {
        chunk_t chunk;
        if (fread(&chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            break;
        }

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            fmt_t fmt;
            uint32_t want = chunk.size < sizeof(fmt) ? chunk.size : (uint32_t)sizeof(fmt);
            if (fread(&fmt, 1, want, file) != want) {
                break;
            }
            if (fmt_out) {
                *fmt_out = fmt;
            }
            have_fmt = true;
            if (chunk.size > want) {
                fseek(file, (long)(chunk.size - want), SEEK_CUR);
            }
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            if (data_off) {
                *data_off = (uint32_t)ftell(file);
            }
            if (data_len) {
                *data_len = chunk.size;
            }
            return have_fmt;
        } else {
            fseek(file, (long)((chunk.size + 1) & ~1u), SEEK_CUR);
        }
    }
    return false;
}

static bool is_wav(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot, ".wav") == 0;
}

/* Newest first. When the file system gives no date (or two files share the
 * second), the name breaks the tie, since it carries the sequence number. */
static int by_newest(const void *lhs, const void *rhs)
{
    const rec_file_t *a = (const rec_file_t *)lhs;
    const rec_file_t *b = (const rec_file_t *)rhs;

    if (a->mtime != b->mtime) {
        return a->mtime < b->mtime ? 1 : -1;
    }
    return strcmp(b->name, a->name);
}

void rec_files_path(char *out, size_t out_len, const char *name)
{
    snprintf(out, out_len, "%s/%s", aos_hal_path_recordings(), name);
}

int rec_files_scan(rec_file_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }

    DIR *dir = opendir(aos_hal_path_recordings());
    if (!dir) {
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max) {
        if (entry->d_name[0] == '.' || !is_wav(entry->d_name)) {
            continue;
        }

        rec_file_t *item = &out[count];
        memset(item, 0, sizeof(*item));
        /* d_name may carry up to 255 characters; the name is clipped. */
        snprintf(item->name, sizeof(item->name), "%.*s",
                 (int)(sizeof(item->name) - 1), entry->d_name);

        char path[REC_PATH_LEN];
        rec_files_path(path, sizeof(path), item->name);

        struct stat info;
        if (stat(path, &info) == 0) {
            item->bytes = (uint32_t)info.st_size;
            item->mtime = info.st_mtime;
        }

        FILE *file = fopen(path, "rb");
        if (file) {
            fmt_t fmt;
            uint32_t data_len = 0;
            if (wav_probe(file, &fmt, NULL, &data_len) && fmt.byte_rate) {
                item->sample_rate = fmt.sample_rate;
                item->seconds     = data_len / fmt.byte_rate;
            }
            fclose(file);
        }

        /* A WAV missing its data chunk (a recording cut short badly) is listed
         * all the same: it appears as 0:00 and can be deleted. */
        count++;
    }
    closedir(dir);

    qsort(out, (size_t)count, sizeof(rec_file_t), by_newest);
    return count;
}

void rec_files_next_name(char *out, size_t out_len)
{
    int highest = 0;

    DIR *dir = opendir(aos_hal_path_recordings());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!is_wav(entry->d_name)) {
                continue;
            }
            const char *underscore = strrchr(entry->d_name, '_');
            int number = underscore ? atoi(underscore + 1) : 0;
            if (number > highest) {
                highest = number;
            }
        }
        closedir(dir);
    }

    snprintf(out, out_len, "grabacion_%04d.wav", highest + 1);
}

void rec_files_pretty(char *out, size_t out_len, const char *name)
{
    snprintf(out, out_len, "%s", name);

    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }
    for (char *p = out; *p; p++) {
        if (*p == '_') {
            *p = ' ';
        }
    }
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
}

bool rec_files_delete(const char *name)
{
    char path[REC_PATH_LEN];
    rec_files_path(path, sizeof(path), name);
    return remove(path) == 0;
}

bool rec_files_envelope(const char *name, uint8_t *out, int bars)
{
    if (!out || bars <= 0) {
        return false;
    }
    memset(out, 0, (size_t)bars);

    char path[REC_PATH_LEN];
    rec_files_path(path, sizeof(path), name);

    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }

    fmt_t fmt;
    uint32_t data_off = 0, data_len = 0;
    if (!wav_probe(file, &fmt, &data_off, &data_len) || fmt.bits != 16 || !data_len) {
        fclose(file);
        return false;
    }

    const int window = 256;                 /* samples inspected per bar */
    int16_t buffer[256];
    uint32_t frames = data_len / (uint32_t)(fmt.block_align ? fmt.block_align : 2);
    uint32_t step   = frames / (uint32_t)bars;
    if (step == 0) {
        step = 1;
    }

    for (int bar = 0; bar < bars; bar++) {
        uint32_t frame = step * (uint32_t)bar;
        fseek(file, (long)(data_off + frame * fmt.block_align), SEEK_SET);

        size_t got = fread(buffer, sizeof(int16_t), (size_t)window, file);
        int32_t peak = 0;
        for (size_t i = 0; i < got; i += fmt.channels ? fmt.channels : 1) {
            int32_t value = buffer[i] < 0 ? -buffer[i] : buffer[i];
            if (value > peak) {
                peak = value;
            }
        }
        int level = (int)(peak * 100 / 32768);
        out[bar] = (uint8_t)(level > 100 ? 100 : level);
    }

    fclose(file);
    return true;
}

void rec_files_when(char *out, size_t out_len, time_t when)
{
    if (when == 0) {
        snprintf(out, out_len, "%s", _("sin fecha"));
        return;
    }

    struct tm stamp;
    localtime_r(&when, &stamp);

    struct tm today;
    aos_hal_time_now(&today);

    /* Approximate Julian days: enough to tell today from yesterday without
     * dragging in mktime or worrying about the time zone. */
    int day_now  = today.tm_year * 366 + today.tm_yday;
    int day_then = stamp.tm_year * 366 + stamp.tm_yday;

    if (day_now == day_then) {
        snprintf(out, out_len, "hoy %02d:%02d", stamp.tm_hour, stamp.tm_min);
    } else if (day_now - day_then == 1) {
        snprintf(out, out_len, "ayer %02d:%02d", stamp.tm_hour, stamp.tm_min);
    } else {
        snprintf(out, out_len, "%d %s %02d:%02d", stamp.tm_mday,
                 aos_month_name(stamp.tm_mon), stamp.tm_hour, stamp.tm_min);
    }
}

void rec_files_size(char *out, size_t out_len, uint32_t bytes)
{
    if (bytes >= 1024 * 1024) {
        uint32_t tenths = (bytes * 10 / (1024 * 1024)) % 10;
        snprintf(out, out_len, "%u,%u MB", (unsigned)(bytes / (1024 * 1024)),
                 (unsigned)tenths);
    } else {
        snprintf(out, out_len, "%u KB", (unsigned)(bytes / 1024));
    }
}
