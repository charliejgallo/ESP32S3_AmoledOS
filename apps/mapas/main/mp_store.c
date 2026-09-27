/*
 * MAPAS - the tile store. See mp_store.h.
 */
#include "mp_mem.h"
#include "mp_store.h"
#include "aos_hal.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_RAM     96
#define MAX_ABSENT  64

typedef struct {
    mp_tile_t *t;
    uint8_t    z;
    uint32_t   x, y;
    uint32_t   stamp;
} ram_t;

typedef struct {
    uint8_t  z, pad[3];
    uint32_t x, y, off, len;
} pidx_t;

typedef struct {
    mp_pack_info_t info;
    pidx_t        *idx;
} pack_t;

static ram_t    s_ram[MAX_RAM];
static uint32_t s_bytes, s_budget, s_stamp;
static pack_t   s_packs[MP_MAX_PACKS];
static int      s_npacks;
static uint32_t s_hits_pack, s_hits_cache;

/* keys known to have nothing on the card (not in a pack, not cached), so a
 * render does not stat the card for them again and again. Cleared when a
 * tile arrives or the packs change. */
static struct { uint8_t z; uint32_t x, y; } s_absent[MAX_ABSENT];
static int s_nabsent, s_absent_next;

const char *mp_maps_dir(void)
{
    static char dir[96];
    const char *root = aos_hal_path_sd_root();
    if (!root) return NULL;
    if (!dir[0]) {
        snprintf(dir, sizeof dir, "%s/maps", root);
        mkdir(dir, 0777);
    }
    return dir;
}

void mp_store_init(uint32_t budget)
{
    memset(s_ram, 0, sizeof s_ram);
    s_bytes = 0;
    s_budget = budget;
    s_stamp = 1;
    s_nabsent = s_absent_next = 0;
    s_hits_pack = s_hits_cache = 0;
}

static void packs_free(void)
{
    for (int i = 0; i < s_npacks; i++) mp_free(s_packs[i].idx);
    s_npacks = 0;
}

void mp_store_deinit(void)
{
    for (int i = 0; i < MAX_RAM; i++) {
        mp_tile_free(s_ram[i].t);
        s_ram[i].t = NULL;
    }
    s_bytes = 0;
    packs_free();
}

/* ---------------------------------------------------------------------------
 * Packs
 * ------------------------------------------------------------------------- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool pack_open(pack_t *pk, const char *dir, const char *file)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    uint8_t h[64];
    bool ok = fread(h, 1, 64, fp) == 64 && memcmp(h, "AMP1", 4) == 0;
    uint32_t n = ok ? rd32(h + 4) : 0;
    uint32_t io = ok ? rd32(h + 60) : 0;
    ok = ok && n > 0 && n < 200000 && io >= 64;
    if (ok) {
        pk->idx = (pidx_t *)mp_malloc((size_t)n * sizeof(pidx_t));
        ok = pk->idx && fseek(fp, (long)io, SEEK_SET) == 0 &&
             fread(pk->idx, sizeof(pidx_t), n, fp) == n;
    }
    fclose(fp);
    if (!ok) {
        mp_free(pk->idx);
        pk->idx = NULL;
        return false;
    }
    mp_pack_info_t *in = &pk->info;
    memset(in, 0, sizeof *in);
    in->ntiles = (int)n;
    in->minz = h[8];
    in->maxz = h[9];
    in->w = (int32_t)rd32(h + 12);
    in->s = (int32_t)rd32(h + 16);
    in->e = (int32_t)rd32(h + 20);
    in->n = (int32_t)rd32(h + 24);
    memcpy(in->name, h + 28, 31);
    in->name[31] = 0;
    snprintf(in->file, sizeof in->file, "%.47s", file);
    if (!in->name[0]) {
        snprintf(in->name, sizeof in->name, "%.31s", file);
        char *dot = strrchr(in->name, '.');
        if (dot) *dot = 0;
    }
    return true;
}

void mp_store_scan_packs(void)
{
    packs_free();
    s_nabsent = 0;
    const char *dir = mp_maps_dir();
    if (!dir) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && s_npacks < MP_MAX_PACKS) {
        const char *dot = strrchr(e->d_name, '.');
        if (e->d_name[0] == '.' || !dot || strcasecmp(dot, ".amp")) continue;
        if (pack_open(&s_packs[s_npacks], dir, e->d_name)) {
            aos_hal_log("mapas", "pack %s: %d tiles, z%d-%d", e->d_name,
                        s_packs[s_npacks].info.ntiles, s_packs[s_npacks].info.minz,
                        s_packs[s_npacks].info.maxz);
            s_npacks++;
        }
    }
    closedir(d);
}

int mp_store_packs(mp_pack_info_t *out, int max)
{
    int n = s_npacks < max ? s_npacks : max;
    for (int i = 0; i < n; i++) out[i] = s_packs[i].info;
    return n;
}

static int key_cmp(const pidx_t *e, int z, uint32_t x, uint32_t y)
{
    if (e->z != z) return e->z < z ? -1 : 1;
    if (e->x != x) return e->x < x ? -1 : 1;
    if (e->y != y) return e->y < y ? -1 : 1;
    return 0;
}

static uint8_t *read_at(const char *path, uint32_t off, uint32_t len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    uint8_t *buf = (uint8_t *)mp_malloc(len ? len : 1);
    bool ok = buf && fseek(fp, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, fp) == len;
    fclose(fp);
    if (!ok) {
        mp_free(buf);
        return NULL;
    }
    return buf;
}

static mp_tile_t *from_packs(int z, uint32_t x, uint32_t y)
{
    const char *dir = mp_maps_dir();
    for (int i = 0; i < s_npacks && dir; i++) {
        pack_t *pk = &s_packs[i];
        if (z < pk->info.minz || z > pk->info.maxz) continue;
        int lo = 0, hi = pk->info.ntiles - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int c = key_cmp(&pk->idx[mid], z, x, y);
            if (c == 0) {
                char path[160];
                snprintf(path, sizeof path, "%s/%s", dir, pk->info.file);
                uint32_t len = pk->idx[mid].len;
                uint8_t *buf = read_at(path, pk->idx[mid].off, len);
                if (!buf) return NULL;
                mp_tile_t *t = mp_mvt_decode(buf, (int)len, z, x, y);
                mp_free(buf);
                if (t) s_hits_pack++;
                return t;
            }
            if (c < 0) lo = mid + 1;
            else hi = mid - 1;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * The card cache
 * ------------------------------------------------------------------------- */

static void cache_path(char *out, size_t n, int z, uint32_t x, uint32_t y)
{
    snprintf(out, n, "%s/cache/%d/%u/%u.mvt", mp_maps_dir(), z, (unsigned)x, (unsigned)y);
}

static mp_tile_t *from_cache(int z, uint32_t x, uint32_t y)
{
    if (!mp_maps_dir()) return NULL;
    char path[160];
    cache_path(path, sizeof path, z, x, y);
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    uint8_t *buf = read_at(path, 0, (uint32_t)st.st_size);
    if (!buf) return NULL;
    mp_tile_t *t = mp_mvt_decode(buf, (int)st.st_size, z, x, y);
    mp_free(buf);
    if (t) s_hits_cache++;
    return t;
}

static void cache_write(int z, uint32_t x, uint32_t y, const uint8_t *buf, int len)
{
    const char *dir = mp_maps_dir();
    if (!dir) return;
    char path[160];
    snprintf(path, sizeof path, "%s/cache", dir);
    mkdir(path, 0777);
    snprintf(path, sizeof path, "%s/cache/%d", dir, z);
    mkdir(path, 0777);
    snprintf(path, sizeof path, "%s/cache/%d/%u", dir, z, (unsigned)x);
    mkdir(path, 0777);
    char fin[160], tmp[168];
    cache_path(fin, sizeof fin, z, x, y);
    snprintf(tmp, sizeof tmp, "%s.tmp", fin);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) return;
    bool ok = len == 0 || fwrite(buf, 1, (size_t)len, fp) == (size_t)len;
    ok = fclose(fp) == 0 && ok;
    if (ok) {
        remove(fin);
        ok = rename(tmp, fin) == 0;
    }
    if (!ok) remove(tmp);
}

static int rm_tree(const char *path)
{
    int n = 0;
    DIR *d = opendir(path);
    if (!d) return 0;
    struct dirent *e;
    char sub[300];
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
            n += rm_tree(sub);      /* the empty folders stay: no rmdir for apps */
        } else if (remove(sub) == 0) {
            n++;
        }
    }
    closedir(d);
    return n;
}

int mp_store_clear_cache(void)
{
    const char *dir = mp_maps_dir();
    if (!dir) return 0;
    char path[160];
    snprintf(path, sizeof path, "%s/cache", dir);
    return rm_tree(path);
}

/* ---------------------------------------------------------------------------
 * RAM
 * ------------------------------------------------------------------------- */

void mp_store_frame(void)
{
    s_stamp++;
}

static ram_t *ram_find(int z, uint32_t x, uint32_t y)
{
    for (int i = 0; i < MAX_RAM; i++) {
        ram_t *r = &s_ram[i];
        if (r->t && r->z == z && r->x == x && r->y == y) return r;
    }
    return NULL;
}

static void ram_insert(mp_tile_t *t, int z, uint32_t x, uint32_t y)
{
    /* room: evict the least recently used, never one of this render's */
    for (;;) {
        int used = 0, lru = -1, freei = -1;
        for (int i = 0; i < MAX_RAM; i++) {
            if (!s_ram[i].t) {
                if (freei < 0) freei = i;
                continue;
            }
            used++;
            if (s_ram[i].stamp != s_stamp && (lru < 0 || s_ram[i].stamp < s_ram[lru].stamp)) lru = i;
        }
        bool full = freei < 0 || s_bytes + t->bytes > s_budget;
        if (!full || lru < 0) {
            if (freei < 0) {
                /* every slot belongs to this render: drop the new one's
                 * oldest peer anyway rather than leak */
                freei = 0;
                s_bytes -= s_ram[0].t->bytes;
                mp_tile_free(s_ram[0].t);
            }
            s_ram[freei].t = t;
            s_ram[freei].z = (uint8_t)z;
            s_ram[freei].x = x;
            s_ram[freei].y = y;
            s_ram[freei].stamp = s_stamp;
            s_bytes += t->bytes;
            (void)used;
            return;
        }
        s_bytes -= s_ram[lru].t->bytes;
        mp_tile_free(s_ram[lru].t);
        s_ram[lru].t = NULL;
    }
}

static bool absent(int z, uint32_t x, uint32_t y)
{
    for (int i = 0; i < s_nabsent; i++)
        if (s_absent[i].z == z && s_absent[i].x == x && s_absent[i].y == y) return true;
    return false;
}

static void absent_add(int z, uint32_t x, uint32_t y)
{
    int i = s_nabsent < MAX_ABSENT ? s_nabsent++ : (s_absent_next++ % MAX_ABSENT);
    s_absent[i].z = (uint8_t)z;
    s_absent[i].x = x;
    s_absent[i].y = y;
}

mp_tile_t *mp_store_peek(int z, uint32_t x, uint32_t y)
{
    ram_t *r = ram_find(z, x, y);
    if (!r) return NULL;
    r->stamp = s_stamp;
    return r->t;
}

mp_tile_t *mp_store_get(int z, uint32_t x, uint32_t y, bool *need_net)
{
    *need_net = false;
    ram_t *r = ram_find(z, x, y);
    if (r) {
        r->stamp = s_stamp;
        return r->t;
    }
    if (!absent(z, x, y)) {
        mp_tile_t *t = from_packs(z, x, y);
        if (!t) t = from_cache(z, x, y);
        if (t) {
            ram_insert(t, z, x, y);
            return t;
        }
        absent_add(z, x, y);
    }
    *need_net = true;
    return NULL;
}

bool mp_store_put(int z, uint32_t x, uint32_t y, uint8_t *mvt, int len, bool save)
{
    int sl = len > 0 ? mp_mvt_strip(mvt, len, mvt) : 0;
    mp_tile_t *t = mp_mvt_decode(mvt, sl, z, x, y);
    if (!t) return false;
    if (save) cache_write(z, x, y, mvt, sl);
    ram_t *r = ram_find(z, x, y);
    if (r) {
        s_bytes -= r->t->bytes;
        mp_tile_free(r->t);
        r->t = NULL;
    }
    ram_insert(t, z, x, y);
    s_nabsent = 0;
    return true;
}

void mp_store_stats(uint32_t *ram_bytes, int *ram_tiles, uint32_t *hits_pack, uint32_t *hits_cache)
{
    int n = 0;
    for (int i = 0; i < MAX_RAM; i++) n += s_ram[i].t != NULL;
    *ram_bytes = s_bytes;
    *ram_tiles = n;
    *hits_pack = s_hits_pack;
    *hits_cache = s_hits_cache;
}
