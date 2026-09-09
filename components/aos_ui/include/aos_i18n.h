/*
 * AmoledOS - translation
 *
 * Spanish is the source language: it lives in the code, and every other
 * language is a pack on the microSD. The catalog key is the Spanish string
 * itself, gettext style, so marking a string up is wrapping it rather than
 * inventing an identifier for it:
 *
 *     aos_ui_toast(_("Sin conexion"), 1600);
 *
 * With no pack loaded aos_tr() returns its own argument, so the system behaves
 * exactly as it did before any of this existed. No card means Spanish, which
 * is the right fallback for free.
 *
 * A pack is a directory you copy to the card:
 *
 *     /sdcard/lang/en/
 *         meta.txt            display name and format version
 *         _sistema.lang       the firmware's strings, plus every app NAME
 *         demo.2043.lang      one catalog per app, named after aos_app_desc_t.id
 *         aos.clima.lang
 *
 * App names go in _sistema.lang and not in each app's catalog, because the
 * launcher draws "Gemas" without having opened Gemas - that app's catalog is
 * not loaded yet. Since the key is the Spanish text, having "Gemas" in the
 * system catalog is enough, and it works for built-in and dynamic apps alike
 * without either of them knowing about translation at all.
 *
 * Two catalogs are live at once: the system one, always, and the front app's,
 * loaded by the runtime when the app opens and freed when it closes. aos_tr()
 * tries the app, then the system, then gives up and returns its argument.
 *
 * This lives in aos_ui rather than in a component of its own on purpose. The
 * dynamic apps already see aos_ui/include, aos_ui is already in the symbol
 * generator's library list and already globbed by the simulator build, so
 * aos_tr() reaches everything that needs it without four separate build-system
 * edits - each of which is a place to forget something. See docs/I18N.md.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Translate. Never returns NULL: with no match, or no pack loaded, the
 * argument comes back unchanged.
 *
 * The returned pointer is owned by the catalog and stays valid until the
 * language changes or the front app closes. Every LVGL text setter copies
 * immediately, so that is long enough for normal use - just do not stash the
 * pointer across a language switch. */
const char *aos_tr(const char *es);

/* Marking macro. Guarded because '_' is a legal identifier in C and some
 * third-party header could plausibly have claimed it first. */
#ifndef _
#define _(s) aos_tr(s)
#endif

/* Marks a string for extraction WITHOUT translating it here.
 *
 * aos_tr() is a function call, so it cannot appear in a static initializer -
 * a table like
 *
 *     static const city_t CITIES[] = { { N_("Nueva York"), "EST5EDT,..." }, ... };
 *
 * has to store the Spanish and translate at the point of use, with _() where
 * the name is actually drawn. N_ expands to nothing at all; it exists purely
 * so tools/gen_lang.py can see the string and put it in the catalog.
 *
 * Always in pairs: N_ at the table, _ at the draw site. N_ alone puts a key
 * in the catalog that nothing ever looks up; _ alone on a table entry does
 * not compile, which at least tells you. */
#ifndef N_
#define N_(s) (s)
#endif

/* Same, for a string that needs disambiguating.
 *
 * The key is the Spanish text, which breaks down when one Spanish word is two
 * different things: "MAR" is both martes and marzo, and English wants "TUE"
 * for one and "MAR" for the other. The context is glued to the key with a
 * 0x04 byte, exactly as gettext does it, so the two live in the catalog as
 * separate entries:
 *
 *     dia\x04MAR   TUE
 *     mes\x04MAR   MAR
 *
 * With no match it returns 'es' rather than falling back to the plain key -
 * a bare "MAR" in the catalog is by definition the *other* meaning, and
 * showing the wrong month name is worse than showing Spanish. */
const char *aos_trc(const char *ctx, const char *es);

#ifndef C_
#define C_(ctx, s) aos_trc(ctx, s)
#endif

/* N_ with a context: mark in the table, translate with C_ at the draw site. */
#ifndef NC_
#define NC_(ctx, s) (s)
#endif

/* Loads whatever language preferences remember. Called from aos_ui_init(),
 * after the HAL is up: it needs the filesystem and the preference store. */
void aos_i18n_init(void);

#define AOS_LANG_CODE_MAX   12
#define AOS_LANG_NAME_MAX   28
#define AOS_LANG_MAX        8

typedef struct {
    char code[AOS_LANG_CODE_MAX];   /* directory name under /lang: "en", "pt" */
    char name[AOS_LANG_NAME_MAX];   /* display name, from meta.txt            */
    int  strings;                   /* entries in the pack's system catalog   */
    int  apps;                      /* per-app catalogs the pack ships        */
    /* Three states and not a bool: Spanish is neither of the other two. It has
     * no catalog anywhere, it is the text in the source, and saying "card"
     * about it is simply false. */
    enum {
        AOS_LANG_SOURCE = 0,        /* Spanish - the language the code is in  */
        AOS_LANG_CARD,              /* a pack under /sdcard/lang/<code>/      */
        AOS_LANG_EMBEDDED,          /* built into the firmware image          */
    } origin;
} aos_lang_t;

/* Lists the languages available. Spanish is always entry 0 and is not a pack:
 * it is the language in the source. Then the packs on the card, then the ones
 * built into the firmware that the card did not already provide - so a card
 * pack shadows the built-in one of the same code, matching the load order.
 * Returns how many entries were written.
 *
 * Callers should show .origin, because a card pack silently overriding the
 * built-in English is confusing exactly when you are debugging a translation. */
int aos_i18n_scan(aos_lang_t *out, int max);

/* Currently selected language code. "es" when no pack is loaded. */
const char *aos_i18n_current(void);

/* Switches language: loads that pack's system catalog, or unloads everything
 * for "es". Saves the choice to preferences. Returns false if the pack could
 * not be read, in which case the previous language stays.
 *
 * This frees the strings the visible UI is built from, so it must NOT be
 * called from inside an LVGL event callback. Settings defers it the same way
 * it defers opening the watch-face picker. */
bool aos_i18n_set(const char *code);

/* Per-app catalog, keyed by aos_app_desc_t.id ("demo.2043"). The runtime
 * calls these around an app's lifetime; apps never do. Loading when no pack
 * is active, or when the pack ships no catalog for that app, is a no-op. */
void aos_i18n_app_load(const char *app_id);
void aos_i18n_app_unload(void);

/* For the Settings screen: how many strings each live catalog holds. */
int aos_i18n_count(void);
int aos_i18n_app_count(void);

#ifdef __cplusplus
}
#endif
