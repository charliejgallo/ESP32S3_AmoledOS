/*
 * AmoledOS - The firmware's next version, from the watch itself.
 *
 * Each release carries a small file, amoledos-update.json, that says which
 * version it is and where its image is, with its size and its SHA-256
 * (tools/gen_update.py writes it). This reads the one of the latest
 * PUBLISHED release, says whether it is newer than the firmware running, and
 * when asked downloads the image straight into the idle OTA slot: hashed as
 * it arrives, and set to boot only when the size and the hash are the ones
 * published. Restarting is a tap of the person's; nothing installs or
 * restarts by itself.
 *
 * The new image boots on trial like one from POST /api/ota (main.c confirms
 * it at 30 s up, a crash before that brings the previous one back), and the
 * previous one stays in the other slot: aos_update_rollback() goes back to it.
 *
 * Settings -> Update shows it at the watch; GET/POST /api/update in the
 * portal. One job at a time, in a task of its own: every call here returns
 * at once.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The latest published release's: a draft or a pre-release reaches nobody. */
#define AOS_UPDATE_URL_DEFAULT \
    "https://github.com/charliejgallo/ESP32S3_AmoledOS/releases/latest/download/amoledos-update.json"

typedef enum {
    AOS_UPD_IDLE = 0,
    AOS_UPD_CHECKING,       /* reading the manifest */
    AOS_UPD_DOWNLOADING,    /* the image, into the idle slot */
    AOS_UPD_READY,          /* written and checked: it runs after a restart */
    AOS_UPD_FAILED,         /* the last job failed: 'err' and 'why' say how */
} aos_upd_state_t;

/* How it failed, for the screen to say in the person's language. */
typedef enum {
    AOS_UPD_E_NONE = 0,
    AOS_UPD_E_OTHER,
    AOS_UPD_E_NO_NETWORK,   /* the watch is not on a network */
    AOS_UPD_E_NETWORK,      /* could not download: cut, refused, silent */
    AOS_UPD_E_NOT_THERE,    /* the release has no amoledos-update.json (404) */
    AOS_UPD_E_CLOCK,        /* no time yet: a certificate cannot be checked */
    AOS_UPD_E_SERVER,       /* the server's certificate does not verify */
    AOS_UPD_E_MANIFEST,     /* the manifest does not read */
    AOS_UPD_E_UP_TO_DATE,   /* asked to install and nothing newer is offered */
    AOS_UPD_E_DAMAGED,      /* the image is not the one published: size or hash */
    AOS_UPD_E_FLASH,        /* the idle slot could not be written */
    AOS_UPD_E_CANCELLED,
    AOS_UPD_E_MEMORY,
} aos_upd_err_t;

typedef struct {
    aos_upd_state_t state;
    aos_upd_err_t   err;            /* of the last job that failed */
    char            why[120];       /* a line for the log, in English */
    bool            checked;        /* a manifest has been read since boot */
    uint32_t        checked_ago_s;  /* how long ago */
    bool            newer;          /* what it offers is newer than what runs */
    char            ver[32];        /* the version it offers, "" when none */
    char            date[16];
    uint32_t        size;
    int             abi;            /* the apps' ABI of that version, -1 unknown */
    uint32_t        got, total;     /* of the download in hand */
    char            previous[32];   /* the image in the other slot, "" when none */
    bool            custom_source;  /* the manifest is not the official one */
    char            source_host[64];
} aos_update_status_t;

void aos_update_status(aos_update_status_t *out);

/* Fetch the manifest. false when a job is running already. */
bool aos_update_check(void);
/* Download the offered version and leave it ready. false when busy or when
 * nothing newer is offered (check first). */
bool aos_update_install(void);
/* Stop the job in hand; the running firmware is never touched. */
void aos_update_cancel(void);

/* The notes of the version offered in a language ("es", "en", "de"); "" when
 * there are none. Falls back to English, then Spanish. */
void aos_update_notes(const char *lang, char *out, size_t len);

/* Point the watch at a different manifest (a test server, a pre-release);
 * NULL or "" goes back to the official one. Kept across restarts. Only
 * https://, or http:// for a test on the local network. */
bool        aos_update_set_source(const char *url);
const char *aos_update_source(void);

/* Make the image in the other slot the one that boots: the previous version,
 * or the one just downloaded. It is checked whole first; false when it is
 * not a good image. The caller restarts. */
bool aos_update_rollback(void);

/* Whether the release 'offered' is newer than the firmware 'running' (what
 * git describe said when it was built). A release is newer than its own test
 * tags (v0.18.0 after v0.18.0-rc.1) and not than a build some commits after
 * it (v0.18.0-3-g1a2b3c4). */
bool aos_update_newer(const char *offered, const char *running);
