/*
 * AmoledOS - The launcher's order and folders: menu.txt (see aos_menu.h).
 *
 * The file is parsed into two small tables and resolved against the app
 * registry each time the launcher is built, which is rare: at boot, when an
 * app arrives or leaves, and when the portal saves. Resolving is a string
 * search per line against at most AOS_MAX_APPS ids; the launcher logs what
 * its whole build takes, and this is a sliver of it.
 */
#include "aos_menu.h"
#include "aos_ui.h"
#include "aos_hal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lines the file may hold: every app the launcher can hold, each folder's
 * line, and as many again for ids that are not installed right now. */
#define MENU_ENTRIES_MAX    (AOS_MAX_APPS * 2 + AOS_MENU_FOLDERS_MAX)
#define MENU_ID_MAX         40      /* as the loader's dynapp_t.id */
#define MENU_LINE_MAX       192

typedef struct {
    char   id[MENU_ID_MAX];         /* the app's id; empty for a folder line */
    int8_t folder;                  /* app inside this folder, or -1 at the top.
                                     * For a folder line, which folder it is */
    bool   is_folder;
    bool   hidden;                  /* a "hide" line: installed, not shown */
} entry_t;

AOS_BSS_PSRAM static entry_t           s_entries[MENU_ENTRIES_MAX];
AOS_BSS_PSRAM static aos_menu_folder_t s_folders[AOS_MENU_FOLDERS_MAX];
static int s_entry_count;
static int s_folder_count;

/* --------------------------------------------------------------------------
 * Parsing
 *
 * One function for both uses: loading fills the tables, validating (from the
 * portal's task, where the tables must not be touched) passes fill = false
 * and only wants to know whether the text is good.
 * -------------------------------------------------------------------------- */

static bool parse_hex(const char *tok, uint32_t *out)
{
    if (strlen(tok) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)tok[i])) {
            return false;
        }
    }
    *out = (uint32_t)strtoul(tok, NULL, 16);
    return true;
}

/* Copies at most dst_len-1 bytes without cutting a UTF-8 sequence in half. */
static void copy_utf8(char *dst, size_t dst_len, const char *src)
{
    size_t n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Splits off the next whitespace-separated token; returns NULL at the end. */
static char *next_token(char **cursor)
{
    char *p = *cursor;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) {
        *cursor = p;
        return NULL;
    }
    char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) {
        *p++ = '\0';
    }
    *cursor = p;
    return start;
}

static bool fail(char *err, size_t err_len, int line, const char *why)
{
    if (err && err_len) {
        snprintf(err, err_len, "line %d: %s", line, why);
    }
    return false;
}

static bool parse(const char *text, size_t len, bool fill, char *err, size_t err_len)
{
    int entries = 0, folders = 0, open_folder = -1, line_no = 0;
    size_t pos = 0;
    /* Folder ids seen so far, for the duplicate check in both modes. */
    char seen[AOS_MENU_FOLDERS_MAX][AOS_MENU_FOLDER_ID_MAX];

    while (pos < len) {
        char line[MENU_LINE_MAX];
        size_t n = 0;
        line_no++;
        while (pos < len && text[pos] != '\n') {
            if (n + 1 >= sizeof(line)) {
                return fail(err, err_len, line_no, "line too long");
            }
            line[n++] = text[pos++];
        }
        pos++;                              /* the '\n' */
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
            n--;
        }
        line[n] = '\0';

        char *cur = line;
        char *kw = next_token(&cur);
        if (!kw || kw[0] == '#') {
            continue;
        }

        if (strcmp(kw, "app") == 0) {
            char *id = next_token(&cur);
            if (!id) {
                return fail(err, err_len, line_no, "app without an id");
            }
            if (strlen(id) >= MENU_ID_MAX) {
                return fail(err, err_len, line_no, "app id too long");
            }
            if (entries >= MENU_ENTRIES_MAX) {
                return fail(err, err_len, line_no, "too many lines");
            }
            if (fill) {
                entry_t *e = &s_entries[entries];
                snprintf(e->id, sizeof(e->id), "%s", id);
                e->folder = (int8_t)open_folder;
                e->is_folder = false;
                e->hidden = false;
            }
            entries++;

        } else if (strcmp(kw, "hide") == 0) {
            char *id = next_token(&cur);
            if (!id) {
                return fail(err, err_len, line_no, "hide without an id");
            }
            if (strlen(id) >= MENU_ID_MAX) {
                return fail(err, err_len, line_no, "app id too long");
            }
            if (entries >= MENU_ENTRIES_MAX) {
                return fail(err, err_len, line_no, "too many lines");
            }
            if (fill) {
                entry_t *e = &s_entries[entries];
                snprintf(e->id, sizeof(e->id), "%s", id);
                e->folder = -1;
                e->is_folder = false;
                e->hidden = true;
            }
            entries++;

        } else if (strcmp(kw, "folder") == 0) {
            if (open_folder >= 0) {
                return fail(err, err_len, line_no, "a folder inside a folder");
            }
            if (folders >= AOS_MENU_FOLDERS_MAX) {
                return fail(err, err_len, line_no, "too many folders");
            }
            if (entries >= MENU_ENTRIES_MAX) {
                return fail(err, err_len, line_no, "too many lines");
            }
            char *id    = next_token(&cur);
            char *ca    = next_token(&cur);
            char *cb    = next_token(&cur);
            char *fl    = next_token(&cur);
            char *glyph = next_token(&cur);
            char *gc    = next_token(&cur);
            while (*cur == ' ' || *cur == '\t') cur++;
            const char *name = cur;

            uint32_t a, b;
            if (!gc || !*name) {
                return fail(err, err_len, line_no, "folder needs id, two colours, "
                                                   "fill, glyph, glyph colour and a name");
            }
            if (strlen(id) >= AOS_MENU_FOLDER_ID_MAX) {
                return fail(err, err_len, line_no, "folder id too long");
            }
            if (!parse_hex(ca, &a) || !parse_hex(cb, &b)) {
                return fail(err, err_len, line_no, "colours are six hex digits");
            }
            static const char FILLS[] = "svdr";
            const char *f = strlen(fl) == 1 ? strchr(FILLS, fl[0]) : NULL;
            if (!f) {
                return fail(err, err_len, line_no, "fill is s, v, d or r");
            }
            if (strcmp(gc, "w") != 0 && strcmp(gc, "b") != 0) {
                return fail(err, err_len, line_no, "glyph colour is w or b");
            }
            for (int i = 0; i < folders; i++) {
                if (strcmp(seen[i], id) == 0) {
                    return fail(err, err_len, line_no, "two folders with the same id");
                }
            }
            snprintf(seen[folders], sizeof(seen[0]), "%s", id);
            if (fill) {
                aos_menu_folder_t *fo = &s_folders[folders];
                memset(fo, 0, sizeof(*fo));
                snprintf(fo->id, sizeof(fo->id), "%s", id);
                copy_utf8(fo->name, sizeof(fo->name), name);
                snprintf(fo->glyph, sizeof(fo->glyph), "%s", glyph);
                fo->color_a = a;
                fo->color_b = b;
                fo->fill = (uint8_t)(f - FILLS);
                fo->glyph_dark = gc[0] == 'b';

                entry_t *e = &s_entries[entries];
                e->id[0] = '\0';
                e->folder = (int8_t)folders;
                e->is_folder = true;
                e->hidden = false;
            }
            open_folder = folders++;
            entries++;

        } else if (strcmp(kw, "end") == 0) {
            if (open_folder < 0) {
                return fail(err, err_len, line_no, "end without a folder");
            }
            open_folder = -1;

        } else {
            return fail(err, err_len, line_no, "unknown line (app, folder, end, hide)");
        }
    }

    if (open_folder >= 0) {
        return fail(err, err_len, line_no, "the last folder has no end");
    }
    if (fill) {
        s_entry_count = entries;
        s_folder_count = folders;
    }
    return true;
}

bool aos_menu_validate(const char *text, size_t len, char *err, size_t err_len)
{
    if (err && err_len) {
        err[0] = '\0';
    }
    if (len > AOS_MENU_FILE_MAX) {
        return fail(err, err_len, 0, "file too large");
    }
    return parse(text, len, false, err, err_len);
}

int aos_menu_load(void)
{
    s_entry_count = 0;
    s_folder_count = 0;

    const char *path = aos_hal_path_menu();
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;       /* no file: the usual order */
    }
    /* Over 1 KB, so malloc takes it from PSRAM on the board. */
    char *buf = malloc(AOS_MENU_FILE_MAX + 1);
    if (!buf) {
        fclose(f);
        aos_hal_log("menu", "no memory to read %s", path);
        return 0;
    }
    size_t len = fread(buf, 1, AOS_MENU_FILE_MAX + 1, f);
    fclose(f);

    char err[64] = "";
    if (len > AOS_MENU_FILE_MAX) {
        aos_hal_log("menu", "%s is larger than %d bytes, ignored", path, AOS_MENU_FILE_MAX);
    } else if (!parse(buf, len, true, err, sizeof(err))) {
        /* A half-read file would be worse than none: all or nothing. */
        s_entry_count = 0;
        s_folder_count = 0;
        aos_hal_log("menu", "%s ignored, %s", path, err);
    } else {
        aos_hal_log("menu", "%s: %d lines, %d folders", path, s_entry_count, s_folder_count);
    }
    free(buf);
    return s_folder_count;
}

/* --------------------------------------------------------------------------
 * Resolving against what is installed
 * -------------------------------------------------------------------------- */

static int app_index(const char *id)
{
    int n = aos_ui_app_count();
    for (int i = 0; i < n; i++) {
        const aos_app_t *a = aos_ui_app_at(i);
        if (a && a->desc.id && strcmp(a->desc.id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* A "hide" line wins over every other line for the same app, wherever it
 * is in the file: the app counts as placed and shows nowhere. */
static void mark_hidden(bool *placed)
{
    for (int i = 0; i < s_entry_count; i++) {
        if (s_entries[i].hidden) {
            int idx = app_index(s_entries[i].id);
            if (idx >= 0) {
                placed[idx] = true;
            }
        }
    }
}

int aos_menu_root(aos_menu_item_t *out, int max)
{
    /* Which registry slots the file already placed, inside a folder or not. */
    bool placed[AOS_MAX_APPS] = { false };
    int count = 0;
    mark_hidden(placed);

    for (int i = 0; i < s_entry_count; i++) {
        const entry_t *e = &s_entries[i];
        if (e->hidden) {
            continue;
        }
        if (e->is_folder) {
            if (count < max) {
                out[count].app = NULL;
                out[count].folder = e->folder;
                count++;
            }
            continue;
        }
        int idx = app_index(e->id);
        if (idx < 0 || placed[idx]) {
            continue;           /* not installed, or already placed once */
        }
        placed[idx] = true;
        if (e->folder < 0 && count < max) {
            out[count].app = aos_ui_app_at(idx);
            out[count].folder = -1;
            count++;
        }
    }

    int n = aos_ui_app_count();
    for (int i = 0; i < n && count < max; i++) {
        if (!placed[i]) {
            out[count].app = aos_ui_app_at(i);
            out[count].folder = -1;
            count++;
        }
    }
    return count;
}

int aos_menu_folder_apps(int folder, const aos_app_t **out, int max)
{
    /* The same first-line-wins rule as the top level, so an app that the
     * file names twice shows only where the top level expects it. */
    bool placed[AOS_MAX_APPS] = { false };
    int count = 0;
    mark_hidden(placed);
    for (int i = 0; i < s_entry_count; i++) {
        const entry_t *e = &s_entries[i];
        if (e->is_folder || e->hidden) {
            continue;
        }
        int idx = app_index(e->id);
        if (idx < 0 || placed[idx]) {
            continue;
        }
        placed[idx] = true;
        if (e->folder == folder) {
            if (!out) {
                count++;
            } else if (count < max) {
                out[count++] = aos_ui_app_at(idx);
            }
        }
    }
    return count;
}

int aos_menu_folder_count(void)
{
    return s_folder_count;
}

const aos_menu_folder_t *aos_menu_folder(int index)
{
    return (index >= 0 && index < s_folder_count) ? &s_folders[index] : NULL;
}

int aos_menu_folder_find(const char *id)
{
    if (!id || !id[0]) {
        return -1;
    }
    for (int i = 0; i < s_folder_count; i++) {
        if (strcmp(s_folders[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
