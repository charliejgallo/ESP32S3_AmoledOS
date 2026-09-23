/*
 * AmoledOS - The launcher's order and folders (docs/MENU.md).
 *
 * One text file, menu.txt, written by the portal's /menu page and only read
 * here. A line per entry, in the order the launcher shows them:
 *
 *     # comments and blank lines are ignored
 *     app aos.settings
 *     folder f1 7B2FF7 F107A3 v gamepad-variant w Juegos
 *       app demo.turbo
 *       app demo.golf
 *     end
 *
 * folder <id> <color A> <color B> <fill> <glyph> <glyph colour> <name...>
 *   fill          s solid, v vertical, d diagonal, r radial
 *   glyph         a name from the folder glyph catalogue (aos_folder_icon.h)
 *   glyph colour  w white, b black
 *   name          the rest of the line, as the user wrote it
 *
 * What the file does not mention still shows: an installed app that is in no
 * line goes at the end of the top level, in its usual order. That is where a
 * newly installed app lands. And what the file mentions but is not installed
 * is skipped, but it stays in the file: put the app back on the card and it
 * comes back to its place.
 *
 * One level of folders, and an app in one place: a second line for the same
 * id is ignored.
 *
 *     hide demo.hello
 *
 * keeps an installed app out of the launcher altogether, wherever else the
 * file names it. It is still installed: the portal opens it, and deleting
 * the line brings it back.
 */
#pragma once

#include "aos_app.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_MENU_FOLDERS_MAX    32
#define AOS_MENU_FOLDER_ID_MAX  16
#define AOS_MENU_NAME_MAX       40      /* bytes, UTF-8, terminator included */
#define AOS_MENU_GLYPH_MAX      32
#define AOS_MENU_FILE_MAX       (24 * 1024)

typedef enum {
    AOS_MENU_FILL_SOLID = 0,
    AOS_MENU_FILL_VER,
    AOS_MENU_FILL_DIAG,
    AOS_MENU_FILL_RADIAL,
} aos_menu_fill_t;

typedef struct {
    char     id[AOS_MENU_FOLDER_ID_MAX];
    char     name[AOS_MENU_NAME_MAX];
    char     glyph[AOS_MENU_GLYPH_MAX];
    uint32_t color_a;
    uint32_t color_b;
    uint8_t  fill;                      /* aos_menu_fill_t */
    bool     glyph_dark;
} aos_menu_folder_t;

/* One slot of a resolved level: an app, or a folder (app NULL). */
typedef struct {
    const aos_app_t *app;
    int              folder;            /* index for aos_menu_folder(), -1 for an app */
} aos_menu_item_t;

/* Reads menu.txt again. No file is not an error: every app shows in its
 * usual order. Call from the UI task, with the LVGL lock. Returns the number
 * of folders. */
int aos_menu_load(void);

/* The top level, as the launcher shows it: the file's order with the apps
 * that are installed, then every installed app the file does not place. */
int aos_menu_root(aos_menu_item_t *out, int max);

/* A folder's installed apps, in its order. With out NULL, only counts them. */
int aos_menu_folder_apps(int folder, const aos_app_t **out, int max);

int                      aos_menu_folder_count(void);
const aos_menu_folder_t *aos_menu_folder(int index);
int                      aos_menu_folder_find(const char *id);   /* -1 if none */

/* Checks a menu.txt before it is written, for the portal. Touches no state,
 * so any task may call it. On failure 'err' says which line and why. */
bool aos_menu_validate(const char *text, size_t len, char *err, size_t err_len);

#ifdef __cplusplus
}
#endif
