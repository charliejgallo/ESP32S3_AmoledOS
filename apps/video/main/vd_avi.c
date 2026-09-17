/* AmoledOS - Video: MJPEG-in-AVI reader. See vd_avi.h. */
#include "vd_avi.h"

#include <stdlib.h>
#include <string.h>


static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_chunk_header(FILE *f, char id[4], uint32_t *size)
{
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, f) != 8) {
        return false;
    }
    memcpy(id, hdr, 4);
    *size = rd32(hdr + 4);
    return true;
}

/* Walks the chunks between the current position and 'end', descending into
 * every LIST, until the 'movi' list shows up. The main header is picked up
 * on the way. */
static bool walk(vd_avi_t *avi, long end)
{
    while (ftell(avi->file) + 8 <= end) {
        char     id[4];
        uint32_t size;
        long     start = ftell(avi->file);
        if (!read_chunk_header(avi->file, id, &size)) {
            return false;
        }
        long next = start + 8 + (long)size + (size & 1);

        if (memcmp(id, "LIST", 4) == 0) {
            char type[4];
            if (fread(type, 1, 4, avi->file) != 4) {
                return false;
            }
            if (memcmp(type, "movi", 4) == 0) {
                avi->movi_start = start + 12;
                avi->movi_end   = start + 8 + (long)size;
                avi->pos        = avi->movi_start;
                return true;
            }
            if (walk(avi, next)) {
                return true;
            }
        } else if (memcmp(id, "avih", 4) == 0 && size >= 40) {
            uint8_t h[40];
            if (fread(h, 1, 40, avi->file) != 40) {
                return false;
            }
            avi->us_per_frame = rd32(h + 0);
            avi->total_frames = rd32(h + 16);
            avi->width        = (uint16_t)rd32(h + 32);
            avi->height       = (uint16_t)rd32(h + 36);
        }
        if (fseek(avi->file, next, SEEK_SET) != 0) {
            return false;
        }
    }
    return false;
}

bool vd_avi_open(vd_avi_t *avi, const char *path)
{
    memset(avi, 0, sizeof *avi);
    avi->file = fopen(path, "rb");
    if (!avi->file) {
        return false;
    }
    /* No setvbuf: a frame is one fread of tens of KB, which newlib passes
     * straight to the filesystem in one go, so stdio's small buffer only
     * ever serves the 8-byte chunk headers. (setvbuf is also not in the
     * firmware's symbol table, and it would not have bought anything.) */
    uint8_t riff[12];
    if (fread(riff, 1, 12, avi->file) != 12 ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "AVI ", 4) != 0) {
        vd_avi_close(avi);
        return false;
    }
    long end = 8 + (long)rd32(riff + 4);
    if (!walk(avi, end) || avi->us_per_frame == 0) {
        vd_avi_close(avi);
        return false;
    }
    avi->file_pos = ftell(avi->file);
    return true;
}

void vd_avi_close(vd_avi_t *avi)
{
    if (avi->file) {
        fclose(avi->file);
        avi->file = NULL;
    }
}

void vd_avi_rewind(vd_avi_t *avi)
{
    avi->pos        = avi->movi_start;
    avi->next_index = 0;
}

/* Moves the file to avi->pos, and only if it is not there already.
 *
 * Measured on the board (2026-09-16): an fseek to the position the file is
 * already at is NOT free. FatFS walks the cluster chain from the start of
 * the file for any seek that is not strictly forward, so a seek per frame
 * cost 41 ms at frame 30 and 94 ms at frame 170 of a 2.3 MB file, more
 * than the decode itself. Sequential reading needs no seek at all, and a
 * skip is a forward seek, which FatFS continues from the current cluster. */
static bool go_to(vd_avi_t *avi, long pos)
{
    if (avi->file_pos == pos) {
        return true;
    }
    if (fseek(avi->file, pos, SEEK_SET) != 0) {
        return false;
    }
    avi->file_pos = pos;
    return true;
}

int vd_avi_next(vd_avi_t *avi, uint8_t *buf, int cap)
{
    if (!avi->file) {
        return -1;
    }
    if (!go_to(avi, avi->pos)) {
        return -1;
    }
    while (avi->pos + 8 <= avi->movi_end) {
        char     id[4];
        uint32_t size;
        if (!read_chunk_header(avi->file, id, &size)) {
            return -1;
        }
        avi->file_pos += 8;
        if (memcmp(id, "LIST", 4) == 0) {
            /* 'rec ' lists group chunks; walk straight into them. */
            avi->pos += 12;
            if (!go_to(avi, avi->pos)) {
                return -1;
            }
            continue;
        }
        long next = avi->pos + 8 + (long)size + (size & 1);
        bool video = id[0] == '0' && id[1] == '0' &&
                     id[2] == 'd' && (id[3] == 'c' || id[3] == 'b');
        if (video) {
            avi->next_index++;
            int n = -1;
            if (buf && (int)size <= cap) {
                n = (int)fread(buf, 1, size, avi->file);
                avi->file_pos += n;
                if (n != (int)size) {
                    n = -1;
                }
            } else if (!buf) {
                n = (int)size;
            }
            avi->pos = next;
            return n;
        }
        avi->pos = next;
        if (!go_to(avi, avi->pos)) {
            return -1;
        }
    }
    return 0;
}
