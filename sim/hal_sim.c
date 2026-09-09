/*
 * AmoledOS - HAL implementation for the desktop simulator.
 *
 * Everything that is hardware on the board is simulated here: the battery
 * discharges on its own, the IMU does a gentle random walk and the preferences
 * go into a text file. Good enough to design the UI without the board.
 */
#include "aos_hal.h"
#include "aos_notif_internal.h"

/* Forward-declared: aos_hal_init() reads them from the preferences. */
static bool s_bt_enabled = true;
/* The imaginary phone starts already paired, just as the media one starts
 * already connected: what you want to rehearse every day is a notification
 * arriving, not pairing again. The pairing flow is walked from Settings
 * -forget and pair again-, which is how it will really be used. */
static bool s_bt_bonded = true;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <math.h>

#include <SDL2/SDL.h>

#define PREFS_FILE  "sim_fs/prefs.txt"

static uint64_t s_boot_us;
static int      s_brightness = 80;
static int      s_volume     = 60;
static aos_display_state_t s_display_state = AOS_DISPLAY_ACTIVE;
static bool     s_aod_enabled = true;
static int      s_aod_brightness = 10;
static void   (*s_display_cb)(aos_display_state_t state);
static uint64_t s_last_activity_ms;
static float    s_battery    = 78.0f;
static uint32_t s_steps      = 4231;

/* -------------------------------------------------------------------------- */

static void tone_init(void);

static uint64_t now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

bool aos_hal_init(void)
{
    s_boot_us = now_us();
    s_last_activity_ms = 0;

    int32_t saved = 0;
    mkdir("sim_fs", 0755);
    mkdir("sim_fs/photos", 0755);
    mkdir("sim_fs/apps", 0755);
    mkdir("sim_fs/music", 0755);
    mkdir("sim_fs/data", 0755);
    mkdir("sim_fs/recordings", 0755);
    mkdir("sim_fs/redes", 0755);
    mkdir("sim_fs/lang", 0755);

    tone_init();

    if (aos_hal_pref_get_i32("aod", &saved)) {
        s_aod_enabled = (saved != 0);
    }
    if (aos_hal_pref_get_i32("aod_bright", &saved)) {
        s_aod_brightness = (int)saved;
    }
    if (aos_hal_pref_get_i32("bt_on", &saved)) {
        s_bt_enabled = (saved != 0);
    }
    if (aos_hal_pref_get_i32("bt_bond", &saved)) {
        s_bt_bonded = (saved != 0);
    }
    return true;
}

bool aos_hal_lock(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return true;    /* the simulator is single-threaded */
}

void aos_hal_unlock(void) {}

uint64_t aos_hal_uptime_ms(void)
{
    return (now_us() - s_boot_us) / 1000ULL;
}

/* -------------------------------------------------------------------------- */

bool aos_hal_battery_read(aos_battery_t *out)
{
    if (!out) {
        return false;
    }
    /* drops 1% every 30 s so the indicator can be seen moving */
    float drained = (float)aos_hal_uptime_ms() / 30000.0f;
    float level = s_battery - drained;
    if (level < 3.0f) {
        level = 3.0f;
    }

    out->percent     = (int)level;
    out->voltage     = 3.30f + 0.9f * (level / 100.0f);
    out->current     = -142.0f;
    out->temperature = 28.5f;
    out->charging    = false;
    out->usb_present = true;
    return true;
}

/* -------------------------------------------------------------------------- */

/* Simulated posture of the board. Without this the simulator always reports
 * "face up" and there would be no way to test the level's side mode. */
static int s_pose;      /* 0 flat, 1 on edge, 2 face down */

/* Simulated tilt.
 *
 * On the board the accelerometer gives it; here the mouse position within the
 * window gives it, which is the only thing we have with two continuous axes.
 * It only overrides the "resting" posture (pose 0): the other two stay as they
 * always were so the Level app can be looked at in a fixed state. */
static float s_tilt_x, s_tilt_y;
static bool  s_tilt_valid;

void aos_hal_sim_set_tilt(float x, float y)
{
    s_tilt_x = x;
    s_tilt_y = y;
    s_tilt_valid = true;
}

void aos_hal_sim_set_pose(int pose)
{
    static const char *const names[] = { "flat", "on edge", "face down",
                                         "in the hand" };
    s_pose = ((pose % 4) + 4) % 4;
    printf("[hal] board %s\n", names[s_pose]);
}

int aos_hal_sim_get_pose(void)
{
    return s_pose;
}

bool aos_hal_imu_read(aos_imu_t *out)
{
    if (!out) {
        return false;
    }
    float t = (float)aos_hal_uptime_ms() / 1000.0f;

    switch (s_pose) {
    case 1:     /* standing on edge, swaying a few degrees off vertical */
        out->ax = 0.18f * sinf(t * 0.6f);
        out->ay = -0.98f;
        out->az = 0.05f * sinf(t * 0.9f);
        break;
    case 2:     /* face down: +az, measured on the board on 2026-08-28 */
        out->ax = 0.10f * sinf(t * 0.7f);
        out->ay = 0.10f * cosf(t * 0.5f);
        out->az = 1.0f;
        break;
    case 3: {
        /* In the hand, the way a remote is held: the mouse is the wrist.
         *
         * The other three postures date from before the axes were measured on
         * the board and are not touched, because the spirit level and the
         * games are calibrated against them. This one does come from the
         * 2026-08-28 measurement (docs/DECISIONES.md): ax vertical with +ax
         * downwards, ay horizontal with the right at -ay, face up at -az.
         *
         *   roll  = atan2(-ay, ax)     mouse across,  +-90 degrees
         *   pitch = atan2(-az, ax)     mouse up/down, +-90 degrees
         *
         * From those two angles comes a gravity vector of magnitude 1, which
         * is what the sensor would measure with the board still in that
         * posture. */
        float roll  = (s_tilt_valid ? s_tilt_x : 0.0f) * 3.14159265f;
        float pitch = (s_tilt_valid ? s_tilt_y : 0.0f) * 3.14159265f;
        out->ax =  cosf(pitch) * cosf(roll);
        out->ay = -cosf(pitch) * sinf(roll);
        out->az = -sinf(pitch);
        break;
    }

    default:    /* resting, nearly level */
        if (s_tilt_valid) {
            /* The mouse simulates TILTING the resting board, with the axes
             * measured on the board on 2026-08-28 (DECISIONES.md): +ax is
             * downwards on the screen and the right is -ay. So moving the
             * mouse right = lowering the right edge = negative ay, and moving
             * it down = lowering the bottom edge = positive ax.
             *
             * This used to be ax = tilt_x, ay = tilt_y, that is, the mouse
             * crossed with the screen: on the spirit level moving the mouse
             * across moved the bubble up and down, and any game driven by
             * tilting came out rotated 90 degrees in the simulator and
             * straight on the board, which is the worst possible combination
             * for testing. It is not the same as posture 1, which is still
             * wrong on purpose because things are calibrated against it; this
             * one was never calibrated against anything. */
            out->ax =  s_tilt_y;
            out->ay = -s_tilt_x;
        } else {
            out->ax = 0.12f * sinf(t * 0.7f);
            out->ay = 0.12f * cosf(t * 0.5f);
        }
        /* Face UP is -az.
         *
         * It was the other way round, and that was not harmless: the Remoto
         * app recognises "face down" by looking at the sign of az, so with the
         * simulator starting in this posture it fired that gesture by itself
         * the moment it opened. The "on edge" posture (pose 1) also dates from
         * before the measurement -it leaves gravity at -ay, which is lying on
         * its right side, not standing- but that one is NOT touched: the
         * spirit level and the games are calibrated against it. For the board
         * really standing up there is posture 3, "in the hand". */
        out->az = -1.0f;
        break;
    }

    out->gx = 3.0f * sinf(t * 1.3f);
    out->gy = 3.0f * cosf(t * 1.1f);
    out->gz = 0.5f * sinf(t * 0.3f);
    out->temperature = 30.1f;
    return true;
}

aos_orientation_t aos_hal_imu_orientation(void)
{
    switch (s_pose) {
    case 1:  return AOS_ORIENT_UP;
    case 2:  return AOS_ORIENT_FACE_DOWN;
    case 3:  return AOS_ORIENT_UP;
    default: return AOS_ORIENT_FACE_UP;
    }
}

uint32_t aos_hal_imu_steps(void)
{
    return s_steps + (uint32_t)(aos_hal_uptime_ms() / 4000);
}

void aos_hal_imu_steps_reset(void)
{
    s_steps = 0;
    s_boot_us = now_us();
}

/* -------------------------------------------------------------------------- */

void aos_hal_time_now(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

bool aos_hal_time_set(const struct tm *t)
{
    (void)t;
    return false;       /* in the simulator the system clock rules */
}

bool aos_hal_time_is_valid(void)
{
    return true;
}

void aos_hal_timezone_set(const char *tz)
{
    (void)tz;
}

const char *aos_hal_timezone_get(void)
{
    return "ART+3";
}

bool aos_hal_rtc_alarm_set(const struct tm *when)
{
    (void)when;
    return false;
}

void aos_hal_rtc_alarm_clear(void) {}

/* -------------------------------------------------------------------------- */

int aos_hal_brightness_get(void)
{
    return s_brightness;
}

void aos_hal_brightness_set(int percent)
{
    s_brightness = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    printf("[hal] brightness %d%%\n", s_brightness);
}

aos_touch_gesture_t aos_hal_touch_gesture(void)
{
    return AOS_TOUCH_GESTURE_NONE;  /* on the desktop LVGL detects them */
}

aos_display_state_t aos_hal_display_state(void)
{
    return s_display_state;
}

void aos_hal_display_set_state(aos_display_state_t state)
{
    if (state == s_display_state) {
        return;
    }
    s_display_state = state;
    printf("[hal] display -> %s\n",
           state == AOS_DISPLAY_ACTIVE ? "active" :
           state == AOS_DISPLAY_AOD    ? "dimmed" : "off");
    if (s_display_cb) {
        s_display_cb(state);
    }
}

void aos_hal_set_display_state_cb(void (*cb)(aos_display_state_t state))
{
    s_display_cb = cb;
}

void aos_hal_aod_enable(bool enable)
{
    s_aod_enabled = enable;
    aos_hal_pref_set_i32("aod", enable ? 1 : 0);
    if (!enable && s_display_state == AOS_DISPLAY_AOD) {
        aos_hal_display_set_state(AOS_DISPLAY_OFF);
    }
}

bool aos_hal_aod_enabled(void)
{
    return s_aod_enabled;
}

void aos_hal_aod_brightness_set(int percent)
{
    s_aod_brightness = percent < 1 ? 1 : (percent > 50 ? 50 : percent);
    aos_hal_pref_set_i32("aod_bright", s_aod_brightness);
}

int aos_hal_aod_brightness_get(void)
{
    return s_aod_brightness;
}

/* Effective brightness according to the state. The simulator uses it to darken
 * the window so the always-on look can be seen. */
int aos_hal_effective_brightness(void)
{
    switch (s_display_state) {
    case AOS_DISPLAY_AOD: return s_aod_brightness;
    case AOS_DISPLAY_OFF: return 0;
    default:              return s_brightness;
    }
}

void aos_hal_display_on(bool on)
{
    aos_hal_display_set_state(on ? AOS_DISPLAY_ACTIVE : AOS_DISPLAY_OFF);
}

bool aos_hal_display_is_on(void)
{
    return s_display_state != AOS_DISPLAY_OFF;
}

void aos_hal_activity(void)
{
    s_last_activity_ms = aos_hal_uptime_ms();
    aos_hal_display_set_state(AOS_DISPLAY_ACTIVE);
}

/* Same timing policy as the board; called by the simulator's loop. */
void aos_hal_sim_idle_tick(void)
{
    uint64_t idle = aos_hal_uptime_ms() - s_last_activity_ms;

    switch (s_display_state) {
    case AOS_DISPLAY_ACTIVE:
        if (s_aod_enabled && idle > 15000) {
            aos_hal_display_set_state(AOS_DISPLAY_AOD);
        } else if (!s_aod_enabled && idle > 30000) {
            aos_hal_display_set_state(AOS_DISPLAY_OFF);
        }
        break;
    case AOS_DISPLAY_AOD:
        if (idle > 300000) {
            aos_hal_display_set_state(AOS_DISPLAY_OFF);
        }
        break;
    default:
        break;
    }
}
void aos_hal_sleep(void) { printf("[hal] sleep\n"); }
void aos_hal_shutdown(void) { printf("[hal] shutdown\n"); exit(0); }
void aos_hal_reboot(void) { printf("[hal] reboot\n"); }

/* -------------------------------------------------------------------------- */

/* On the desktop the side button is the space bar: sim/main.c watches SDL's
 * events and calls aos_hal_sim_button() with the same sequence the board
 * generates (press / release). */
static void (*s_button_cb)(aos_button_t button, aos_button_action_t action);

void aos_hal_set_button_cb(void (*cb)(aos_button_t button, aos_button_action_t action))
{
    s_button_cb = cb;
}

void aos_hal_sim_button(int action)
{
    if (s_button_cb) {
        s_button_cb(AOS_BUTTON_BOOT, (aos_button_action_t)action);
    }
}

const char *aos_hal_path_apps(void)   { return "sim_fs/apps";   }
const char *aos_hal_path_photos(void) { return "sim_fs/photos"; }
const char *aos_hal_path_music(void)  { return "sim_fs/music";  }
const char *aos_hal_path_data(void)   { return "sim_fs/data";   }
const char *aos_hal_path_lang(void)   { return "sim_fs/lang";   }
const char *aos_hal_path_recordings(void) { return "sim_fs/recordings"; }
const char *aos_hal_path_scans(void)  { return "sim_fs/redes";  }

bool aos_hal_sd_present(void) { return true; }

bool aos_hal_sd_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (total_bytes) *total_bytes = 32ULL * 1024 * 1024 * 1024;
    if (free_bytes)  *free_bytes  = 21ULL * 1024 * 1024 * 1024;
    return true;
}

/* --------------------------------------------------------------------------
 * Preferences: a "key=value" text file, one per line.
 * -------------------------------------------------------------------------- */

static bool pref_lookup(const char *key, char *out, size_t out_len)
{
    FILE *file = fopen(PREFS_FILE, "r");
    if (!file) {
        return false;
    }
    char line[256];
    bool found = false;
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
            char *value = line + key_len + 1;
            value[strcspn(value, "\r\n")] = '\0';
            strncpy(out, value, out_len - 1);
            out[out_len - 1] = '\0';
            found = true;
            break;
        }
    }
    fclose(file);
    return found;
}

static bool pref_store(const char *key, const char *value)
{
    char lines[64][256];
    int count = 0;
    size_t key_len = strlen(key);

    FILE *file = fopen(PREFS_FILE, "r");
    if (file) {
        while (count < 64 && fgets(lines[count], sizeof(lines[count]), file)) {
            if (strncmp(lines[count], key, key_len) == 0 && lines[count][key_len] == '=') {
                continue;   /* we replace it */
            }
            count++;
        }
        fclose(file);
    }

    file = fopen(PREFS_FILE, "w");
    if (!file) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        fputs(lines[i], file);
    }
    if (value) {
        fprintf(file, "%s=%s\n", key, value);
    }
    fclose(file);
    return true;
}

bool aos_hal_pref_get_i32(const char *key, int32_t *out)
{
    char buf[64];
    if (!pref_lookup(key, buf, sizeof(buf))) {
        return false;
    }
    *out = (int32_t)strtol(buf, NULL, 10);
    return true;
}

bool aos_hal_pref_set_i32(const char *key, int32_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", (int)value);
    return pref_store(key, buf);
}

bool aos_hal_pref_get_str(const char *key, char *out, size_t out_len)
{
    return pref_lookup(key, out, out_len);
}

bool aos_hal_pref_set_str(const char *key, const char *value)
{
    return pref_store(key, value);
}

bool aos_hal_pref_erase(const char *key)
{
    return pref_store(key, NULL);
}

/* --------------------------------------------------------------------------
 * Tones
 *
 * On the board aos_hal_beep() queues the tone and a separate task plays it;
 * here SDL synthesises it in its audio callback. Both implementations share
 * the only thing that matters from outside: the call returns straight away and
 * the requested notes are played one after another.
 *
 * That it really makes a sound on the Mac is not a luxury: a game's sound is
 * designed by listening to it, and the board only arrives tomorrow.
 *
 * The wave is a sine with a little third harmonic and an exponential decay. It
 * sounds like a bell; a bare square wave sounds like a buzzer.
 * -------------------------------------------------------------------------- */

#define TONE_RATE       22050
#define TONE_QUEUE      24

typedef struct {
    uint16_t freq;
    uint16_t ms;
} tone_note_t;

static SDL_AudioDeviceID s_audio;
static tone_note_t s_tone_q[TONE_QUEUE];
static int   s_tone_head, s_tone_count;
static int   s_tone_left, s_tone_total, s_tone_freq;
static float s_tone_phase;
static bool  s_beep_log;

/* Called from the audio callback, which already holds the device's lock. */
static void tone_next(void)
{
    if (s_tone_count == 0) {
        s_tone_freq = 0;
        s_tone_left = 0;
        return;
    }
    s_tone_freq  = s_tone_q[s_tone_head].freq;
    s_tone_total = s_tone_q[s_tone_head].ms * TONE_RATE / 1000;
    s_tone_left  = s_tone_total;
    s_tone_head  = (s_tone_head + 1) % TONE_QUEUE;
    s_tone_count--;
    s_tone_phase = 0.0f;
}

static void tone_callback(void *user_data, Uint8 *stream, int len)
{
    (void)user_data;
    int16_t *out = (int16_t *)stream;
    int count = len / (int)sizeof(int16_t);
    float vol = (float)s_volume / 100.0f;

    for (int i = 0; i < count; i++) {
        if (s_tone_left <= 0) {
            tone_next();
        }
        if (s_tone_left <= 0 || s_tone_total <= 0) {
            out[i] = 0;
            continue;
        }

        float t = 1.0f - (float)s_tone_left / (float)s_tone_total;
        float env = (t < 0.04f) ? (t / 0.04f) : expf(-3.5f * (t - 0.04f));

        s_tone_phase += 6.2831853f * (float)s_tone_freq / (float)TONE_RATE;
        if (s_tone_phase > 6.2831853f) {
            s_tone_phase -= 6.2831853f;
        }
        float v = sinf(s_tone_phase) + 0.22f * sinf(3.0f * s_tone_phase);

        out[i] = (int16_t)(v * env * vol * 9000.0f);
        s_tone_left--;
    }
}

static void tone_init(void)
{
    s_beep_log = (getenv("AOS_SIM_BEEP_LOG") != NULL);

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("[hal] no audio: %s\n", SDL_GetError());
        return;
    }

    SDL_AudioSpec want;
    memset(&want, 0, sizeof(want));
    want.freq     = TONE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;
    want.callback = tone_callback;

    s_audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (s_audio == 0) {
        printf("[hal] could not open the audio device: %s\n", SDL_GetError());
        return;
    }
    SDL_PauseAudioDevice(s_audio, 0);
}

void aos_hal_beep(int freq_hz, int ms)
{
    if (freq_hz <= 0 || ms <= 0) {
        return;
    }
    if (s_beep_log || s_audio == 0) {
        printf("[hal] beep %d Hz %d ms\n", freq_hz, ms);
    }
    if (s_audio == 0) {
        return;
    }

    SDL_LockAudioDevice(s_audio);
    if (s_tone_count < TONE_QUEUE) {
        int idx = (s_tone_head + s_tone_count) % TONE_QUEUE;
        s_tone_q[idx].freq = (uint16_t)freq_hz;
        s_tone_q[idx].ms   = (uint16_t)ms;
        s_tone_count++;
    }
    SDL_UnlockAudioDevice(s_audio);
}

/* --------------------------------------------------------------------------
 * Simulated player: nothing plays, but time advances as if it were playing,
 * which is what is needed to design the interface.
 * -------------------------------------------------------------------------- */
static aos_player_state_t s_player_state;
static char     s_player_path[160];
static char     s_player_title[64];
static uint32_t s_player_duration;
static uint32_t s_player_pos_ms;
static uint64_t s_player_last_ms;

static void player_advance(void)
{
    uint64_t now = aos_hal_uptime_ms();
    if (s_player_state == AOS_PLAYER_PLAYING) {
        s_player_pos_ms += (uint32_t)(now - s_player_last_ms);
        if (s_player_pos_ms >= s_player_duration * 1000) {
            s_player_pos_ms = 0;
            s_player_state = AOS_PLAYER_STOPPED;
        }
    }
    s_player_last_ms = now;
}

bool aos_hal_player_play(const char *path)
{
    if (!path) {
        return false;
    }
    snprintf(s_player_path, sizeof(s_player_path), "%s", path);

    const char *slash = strrchr(path, '/');
    snprintf(s_player_title, sizeof(s_player_title), "%s", slash ? slash + 1 : path);
    char *dot = strrchr(s_player_title, '.');
    if (dot) {
        *dot = '\0';
    }

    /* an invented but stable duration for a given file */
    uint32_t hash = 0;
    for (const char *c = path; *c; c++) {
        hash = hash * 31u + (unsigned char)*c;
    }
    s_player_duration = 90 + (hash % 180);

    s_player_pos_ms = 0;
    s_player_last_ms = aos_hal_uptime_ms();
    s_player_state = AOS_PLAYER_PLAYING;
    printf("[hal] playing %s (%u s)\n", path, (unsigned)s_player_duration);
    return true;
}

void aos_hal_player_pause(void)
{
    player_advance();
    if (s_player_state == AOS_PLAYER_PLAYING) {
        s_player_state = AOS_PLAYER_PAUSED;
    }
}

void aos_hal_player_resume(void)
{
    s_player_last_ms = aos_hal_uptime_ms();
    if (s_player_state == AOS_PLAYER_PAUSED) {
        s_player_state = AOS_PLAYER_PLAYING;
    }
}

void aos_hal_player_stop(void)
{
    s_player_state = AOS_PLAYER_STOPPED;
    s_player_pos_ms = 0;
}

bool aos_hal_player_status(aos_player_status_t *out)
{
    if (!out) {
        return false;
    }
    player_advance();

    out->state       = s_player_state;
    out->duration_s  = s_player_duration;
    out->position_s  = s_player_pos_ms / 1000;
    out->sample_rate = 44100;
    out->channels    = 2;
    snprintf(out->path, sizeof(out->path), "%s", s_player_path);
    snprintf(out->title, sizeof(out->title), "%s", s_player_title);
    return true;
}

bool aos_hal_play_file(const char *path)
{
    return aos_hal_player_play(path);
}

void aos_hal_audio_stop(void) { aos_hal_player_stop(); }
bool aos_hal_audio_is_playing(void) { return s_player_state == AOS_PLAYER_PLAYING; }

/* --------------------------------------------------------------------------
 * Simulated remote control: an imaginary phone with a list of tracks.
 * -------------------------------------------------------------------------- */
static bool s_media_enabled = true;
static bool s_media_playing = true;
static int  s_media_track;
static uint64_t s_media_started_ms;

static const struct { const char *title, *artist, *album; uint32_t len; } MEDIA[] = {
    { "Lullaby",          "The Simulated", "Test Runs", 214 },
    { "Second Track",     "The Simulated", "Test Runs", 187 },
    { "Instrumental",     "Another Band",  "Live",      301 },
};
#define MEDIA_COUNT ((int)(sizeof(MEDIA) / sizeof(MEDIA[0])))

void aos_hal_media_enable(bool enable)
{
    s_media_enabled = enable;
    printf("[hal] media control %s\n", enable ? "on" : "off");
}

bool aos_hal_media_enabled(void)
{
    return s_media_enabled;
}

aos_media_link_t aos_hal_media_link(void)
{
    if (!s_media_enabled) {
        return AOS_MEDIA_OFF;
    }
    /* for the first 4 seconds it pretends to be advertising */
    return aos_hal_uptime_ms() < 4000 ? AOS_MEDIA_ADVERTISING : AOS_MEDIA_CONNECTED;
}

const char *aos_hal_media_peer(void)
{
    return aos_hal_media_link() == AOS_MEDIA_CONNECTED ? "simulated iPhone" : "";
}

const char *aos_hal_media_player(void)
{
    return aos_hal_media_link() == AOS_MEDIA_CONNECTED ? "Musica" : "";
}

bool aos_hal_media_info(aos_media_info_t *out)
{
    if (!out || aos_hal_media_link() != AOS_MEDIA_CONNECTED) {
        return false;
    }
    snprintf(out->title, sizeof(out->title), "%s", MEDIA[s_media_track].title);
    snprintf(out->artist, sizeof(out->artist), "%s", MEDIA[s_media_track].artist);
    snprintf(out->album, sizeof(out->album), "%s", MEDIA[s_media_track].album);
    out->playing      = s_media_playing;
    out->has_metadata = true;
    out->duration_s   = MEDIA[s_media_track].len;
    out->position_s   = s_media_playing
                      ? (uint32_t)((aos_hal_uptime_ms() - s_media_started_ms) / 1000) %
                        MEDIA[s_media_track].len
                      : 0;
    return true;
}

bool aos_hal_media_command(aos_media_cmd_t cmd)
{
    if (aos_hal_media_link() != AOS_MEDIA_CONNECTED) {
        return false;
    }
    switch (cmd) {
    case AOS_MEDIA_PLAY_PAUSE:
        s_media_playing = !s_media_playing;
        s_media_started_ms = aos_hal_uptime_ms();
        break;
    case AOS_MEDIA_NEXT:
        s_media_track = (s_media_track + 1) % MEDIA_COUNT;
        s_media_started_ms = aos_hal_uptime_ms();
        break;
    case AOS_MEDIA_PREV:
        s_media_track = (s_media_track + MEDIA_COUNT - 1) % MEDIA_COUNT;
        s_media_started_ms = aos_hal_uptime_ms();
        break;
    case AOS_MEDIA_VOL_UP:
        aos_hal_volume_set(aos_hal_volume_get() + 5);
        break;
    case AOS_MEDIA_VOL_DOWN:
        aos_hal_volume_set(aos_hal_volume_get() - 5);
        break;
    }
    printf("[hal] media command %d\n", (int)cmd);
    return true;
}

/* --------------------------------------------------------------------------
 * Imaginary phone: bluetooth link and notifications
 *
 * Just like the media control above, but with two more things that have to be
 * rehearsable without the iPhone: pairing with a code, and the flood of
 * notifications iOS dumps on connecting.
 *
 * The test texts deliberately carry emoji, typographic quotes and em dashes.
 * The watch's fonts are Latin-1 plus 61 symbols, and when a glyph is missing
 * LVGL draws NOTHING -not even a little box-, so without these texts phase
 * F2's UTF-8 sanitiser would be written blind and the defect would only turn
 * up with the real phone.
 * -------------------------------------------------------------------------- */

static bool     s_bt_pairing;
static uint32_t s_bt_pair_code;
static uint64_t s_bt_pair_ms;
static uint64_t s_bt_on_ms;
static uint32_t s_bt_next_uid = 1;

static const struct {
    aos_notif_category_t cat;
    const char *app, *title, *msg;
    bool silent, can_act;
} FALSAS[] = {
    { AOS_NOTIF_SOCIAL, "WhatsApp", "Mariana",
      "Shall we go and eat something? \xF0\x9F\x8D\x95 See you at nine",
      false, false },
    { AOS_NOTIF_CALL_INCOMING, "Phone", "Dad",
      "Incoming call", false, true },
    { AOS_NOTIF_EMAIL, "Mail", "Billing \xE2\x80\x94 Payment due",
      "Your monthly invoice is due on the 15th. \xE2\x80\x9C" "Do not reply "
      "to this email\xE2\x80\x9D, it says at the bottom, as always.", false, true },
    { AOS_NOTIF_SCHEDULE, "Calendar", "Meeting in 15 minutes",
      "Firmware review \xE2\x80\x93 small room", false, false },
    { AOS_NOTIF_NEWS, "News", "Breaking",
      "A piece of news you probably did not want, to test the filter by "
      "category.", false, false },
    { AOS_NOTIF_SOCIAL, "Instagram", "you were tagged",
      "\xF0\x9F\x93\xB8 someone tagged you in a photo", true, false },
    { AOS_NOTIF_CALL_MISSED, "Phone", "Missed call",
      "Mariana \xC2\xB7 2 minutes ago", false, false },
    { AOS_NOTIF_OTHER, "System", "A fairly long title, to see how the "
      "full screen behaves",
      "And a message longer still, with several sentences, to verify that the "
      "text is cut where it has to be cut and does not spill outside the 368 "
      "pixels of width this screen has. It has to fit, or be clipped "
      "gracefully, not overflow.", false, false },
};
#define FALSAS_COUNT ((int)(sizeof(FALSAS) / sizeof(FALSAS[0])))

static void empujar_falsa(int i, bool pre_existing)
{
    i %= FALSAS_COUNT;

    aos_notif_t n;
    memset(&n, 0, sizeof(n));
    n.uid          = s_bt_next_uid++;
    n.category     = FALSAS[i].cat;
    n.when         = time(NULL);
    n.silent       = FALSAS[i].silent;
    /* Like the real iPhone: the incoming call offers both actions -answering
     * starts the call on the phone, rejecting hangs it up- and a messaging
     * notification offers only the negative one. */
    n.can_negative = FALSAS[i].can_act;
    n.can_positive = FALSAS[i].can_act &&
                     FALSAS[i].cat == AOS_NOTIF_CALL_INCOMING;
    n.pre_existing = pre_existing;
    snprintf(n.app,     sizeof(n.app),     "%s", FALSAS[i].app);
    snprintf(n.title,   sizeof(n.title),   "%s", FALSAS[i].title);
    snprintf(n.message, sizeof(n.message), "%s", FALSAS[i].msg);

    bool acepto = aos_notif_push(&n);
    printf("[hal] notification #%u %s: %s / %s%s\n",
           (unsigned)n.uid,
           acepto ? (n.pre_existing ? "stored (was already there)" : "accepted")
                  : "DROPPED by the filter",
           n.app, n.title, n.silent ? "  (silent)" : "");
}

/* Called by the simulator's 'n' key. */
void aos_hal_sim_notificacion(void)
{
    static int i;
    if (aos_hal_bt_state() != AOS_BT_CONNECTED) {
        printf("[hal] no phone connected: the notification does not arrive\n");
        return;
    }
    empujar_falsa(i++, false);
}

/* Key 'r': five messages in a row from the same person, which is what WhatsApp
 * really does. Without this, the grouping is written blind. */
void aos_hal_sim_rafaga(void)
{
    if (aos_hal_bt_state() != AOS_BT_CONNECTED) {
        printf("[hal] no phone connected\n");
        return;
    }
    for (int k = 0; k < 5; k++) {
        empujar_falsa(0, false);
    }
}

/* And this one, key 'N': the flood of those already on the phone. */
void aos_hal_sim_notificaciones_previas(void)
{
    if (aos_hal_bt_state() != AOS_BT_CONNECTED) {
        printf("[hal] no phone connected\n");
        return;
    }
    printf("[hal] the phone dumps what it already had pending\n");
    for (int i = 0; i < 4; i++) {
        empujar_falsa(i, true);
    }
}

void aos_hal_bt_enable(bool on)
{
    if (s_bt_enabled == on) {
        return;
    }
    s_bt_enabled = on;
    s_bt_on_ms   = aos_hal_uptime_ms();
    s_bt_pairing = false;
    s_bt_pair_code = 0;
    if (!on) {
        aos_notif_reset_pending();
    }
    aos_hal_pref_set_i32("bt_on", on ? 1 : 0);
    printf("[hal] bluetooth %s\n", on ? "on" : "off");
}

bool aos_hal_bt_enabled(void)
{
    return s_bt_enabled;
}

aos_bt_state_t aos_hal_bt_state(void)
{
    if (!s_bt_enabled) {
        return AOS_BT_OFF;
    }
    if (s_bt_pairing) {
        return AOS_BT_PAIRING;
    }
    if (!s_bt_bonded) {
        return AOS_BT_ADVERTISING;
    }
    /* With the keys stored, the phone takes a couple of seconds to appear.
     * That stretch of ADVERTISING exists so the half-lit icon can be seen in
     * the status bar. */
    return (aos_hal_uptime_ms() - s_bt_on_ms) < 2500 ? AOS_BT_ADVERTISING
                                                     : AOS_BT_CONNECTED;
}

const char *aos_hal_bt_peer(void)
{
    return aos_hal_bt_state() == AOS_BT_CONNECTED ? "simulated iPhone" : "";
}

bool aos_hal_bt_phone_battery(int *percent)
{
    if (aos_hal_bt_state() != AOS_BT_CONNECTED) {
        return false;
    }
    /* Drops one per cent a minute and starts over, so the number can be seen
     * changing without waiting an afternoon. */
    if (percent) {
        *percent = 100 - (int)((aos_hal_uptime_ms() / 60000) % 70);
    }
    return true;
}

bool aos_hal_bt_bonded(void)
{
    return s_bt_bonded;
}

void aos_hal_bt_forget(void)
{
    s_bt_bonded  = false;
    s_bt_pairing = false;
    s_bt_pair_code = 0;
    s_bt_on_ms = aos_hal_uptime_ms();
    aos_hal_pref_set_i32("bt_bond", 0);
    aos_notif_reset_pending();
    printf("[hal] phone forgotten\n");
}

void aos_hal_bt_pair_begin(void)
{
    if (!s_bt_enabled) {
        aos_hal_bt_enable(true);
    }
    s_bt_pairing   = true;
    s_bt_pair_code = 0;
    s_bt_pair_ms   = aos_hal_uptime_ms();
    printf("[hal] waiting for the phone to ask to pair...\n");
}

uint32_t aos_hal_bt_pair_code(void)
{
    if (!s_bt_pairing) {
        return 0;
    }
    /* A second and a half so the "look for AmoledOS on the phone" can be
     * seen. */
    if (!s_bt_pair_code && aos_hal_uptime_ms() - s_bt_pair_ms > 1500) {
        s_bt_pair_code = 100000 + (uint32_t)(aos_hal_uptime_ms() % 900000);
        printf("[hal] the phone shows the code %06u\n",
               (unsigned)s_bt_pair_code);
    }
    return s_bt_pair_code;
}

void aos_hal_bt_pair_confirm(bool accept)
{
    s_bt_pairing   = false;
    s_bt_pair_code = 0;
    if (!accept) {
        printf("[hal] pairing rejected\n");
        return;
    }
    s_bt_bonded = true;
    s_bt_on_ms  = aos_hal_uptime_ms();
    aos_hal_pref_set_i32("bt_bond", 1);
    printf("[hal] paired with the phone\n");
}

void aos_hal_bt_pair_cancel(void)
{
    aos_hal_bt_pair_confirm(false);
}

bool aos_hal_notif_action(uint32_t uid, bool positive)
{
    printf("[hal] %s action on notification #%u\n",
           positive ? "positive (answer / accept)" : "negative (hang up / dismiss)",
           (unsigned)uid);
    aos_notif_push_removed(uid);
    return true;
}

/* --------------------------------------------------------------------------
 * Simulated recorder
 *
 * There is no microphone, but there is a file: the simulator synthesises fake
 * speech (low-pitched syllables with noise on top, separated by silences) and
 * writes it as a 16-bit PCM WAV identical to the one that will come off the
 * board. That way the list, the duration, the waveform and the deletion are
 * really tested, with files that exist and can be opened in any player.
 *
 * The audio is generated when somebody asks: every call to status/peaks
 * advances the recording up to wall-clock time, synthesising whatever blocks
 * are missing. It is the same trick the simulated player uses.
 * -------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t riff_size;
    char     wave[4];
    char     fmt_id[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
    char     data_id[4];
    uint32_t data_size;
} sim_wav_header_t;

#define SIM_REC_RING    256

/* Just as on the board: ONE capture with two consumers, the recorder and the
 * raw microphone. Here the capture is not a task but a lazy generator —
 * mic_advance() manufactures the blocks corresponding to the elapsed time each
 * time somebody asks something — but the shape the app sees is the same. */

static aos_rec_state_t s_rec_state;
static char      s_rec_path[160];
static FILE     *s_rec_file;
static uint32_t  s_rec_rate = AOS_REC_RATE_HZ;
static uint32_t  s_rec_bytes;
static uint32_t  s_rec_flushed;
static uint64_t  s_rec_last_ms;

static uint8_t   s_rec_ring[SIM_REC_RING];
static uint32_t  s_rec_ring_w;
static uint32_t  s_rec_ring_r;

static bool      s_mic_open;
static uint32_t  s_mic_rate = AOS_MIC_RATE_HZ;
static int       s_mic_level;
static int       s_mic_peak;
static int       s_mic_gain_db = 30;

static int16_t  *s_pcm_ring;
static uint32_t  s_pcm_len;
static uint32_t  s_pcm_w;
static uint32_t  s_pcm_r;
static uint32_t  s_pcm_dropped;

/* state of the synthesiser */
static float     s_syn_env;
static float     s_syn_target;
static int       s_syn_left;
static float     s_syn_phase;

static int rec_level(void) { return s_mic_level; }

static void rec_header_write(FILE *file, uint32_t rate, uint32_t data_bytes)
{
    sim_wav_header_t header = {
        .riff = {'R','I','F','F'}, .riff_size = 36 + data_bytes,
        .wave = {'W','A','V','E'},
        .fmt_id = {'f','m','t',' '}, .fmt_size = 16,
        .format = 1, .channels = 1, .sample_rate = rate,
        .byte_rate = rate * 2, .block_align = 2, .bits = 16,
        .data_id = {'d','a','t','a'}, .data_size = data_bytes,
    };
    fseek(file, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, file);
}

/* Same scale as on the board: the VU goes in dB, not in linear. A normal voice
 * peaks at ~4000 of 32768, which in linear would be 12 out of 100 and the
 * waveform would never leave the baseline. -48 dBFS..0 dBFS mapped to
 * 0..100. */
static int rec_level_from_peak(int32_t peak)
{
    if (peak < 16) {
        return 0;
    }
    float db = 20.0f * log10f((float)peak / 32768.0f);
    if (db < -48.0f) {
        return 0;
    }
    int level = (int)((db + 48.0f) * (100.0f / 48.0f) + 0.5f);
    return level > 100 ? 100 : level;
}

/* Synthesises a block of 1/AOS_REC_PEAK_HZ seconds into 'buffer' and returns
 * its raw peak. It writes neither to the file nor to any ring: that is
 * mic_advance()'s job, which is the one that knows who is listening.
 *
 * By default it imitates speech (syllables with pauses). With MIC_TONE=440 it
 * generates a tone with two harmonics, which is what is needed to test a tuner
 * against a known value without whistling at the Mac. */
static int mic_synth_block(int16_t *buffer, int samples)
{
    static float tone_hz = -1.0f;
    if (tone_hz < 0.0f) {
        const char *env = getenv("MIC_TONE");
        tone_hz = env ? (float)atof(env) : 0.0f;
        if (tone_hz > 0.0f) {
            printf("[hal] synthetic microphone: %.1f Hz tone\n", tone_hz);
        }
    }

    int peak = 0;

    if (tone_hz > 0.0f) {
        for (int i = 0; i < samples; i++) {
            s_syn_phase += 2.0f * (float)M_PI * tone_hz / (float)s_mic_rate;
            float value = 0.70f * sinf(s_syn_phase)
                        + 0.20f * sinf(2.0f * s_syn_phase)
                        + 0.10f * sinf(3.0f * s_syn_phase);
            /* a little noise, so the detection does not have it too easy */
            value += ((float)(rand() % 200) - 100.0f) / 4000.0f;
            int32_t sample = (int32_t)(value * 9000.0f);
            if (sample > 32767)  sample = 32767;
            if (sample < -32768) sample = -32768;
            buffer[i] = (int16_t)sample;
            int32_t magnitude = sample < 0 ? -sample : sample;
            if (magnitude > peak) {
                peak = (int)magnitude;
            }
        }
        return peak;
    }

    if (s_syn_left <= 0) {
        if (s_syn_target > 0.2f) {              /* was talking: fall silent */
            s_syn_left   = 2 + rand() % 5;
            s_syn_target = 0.02f + (float)(rand() % 60) / 1000.0f;
        } else {                                /* start another syllable */
            s_syn_left   = 3 + rand() % 6;
            s_syn_target = 0.35f + (float)(rand() % 60) / 100.0f;
        }
    }
    s_syn_left--;
    s_syn_env += (s_syn_target - s_syn_env) * 0.45f;

    const float freq = 130.0f + (float)(rand() % 40);
    for (int i = 0; i < samples; i++) {
        s_syn_phase += 2.0f * (float)M_PI * freq / (float)s_mic_rate;
        float noise = ((float)(rand() % 2000) - 1000.0f) / 1000.0f;
        float value = s_syn_env * (0.65f * sinf(s_syn_phase) + 0.35f * noise);
        int32_t sample = (int32_t)(value * 26000.0f);
        if (sample > 32767)  sample = 32767;
        if (sample < -32768) sample = -32768;
        buffer[i] = (int16_t)sample;
        int32_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > peak) {
            peak = (int)magnitude;
        }
    }
    return peak;
}

static void pcm_push(const int16_t *samples, int count)
{
    if (!s_pcm_ring || !s_pcm_len) {
        return;
    }
    for (int i = 0; i < count; i++) {
        s_pcm_ring[(s_pcm_w + (uint32_t)i) % s_pcm_len] = samples[i];
    }
    s_pcm_w += (uint32_t)count;
}

/* Brings the capture up to date: manufactures the blocks corresponding to the
 * elapsed time and hands them out to whoever is listening. Called from every
 * entry point of the API, which is what saves the simulator from needing a
 * thread. */
static void mic_advance(void)
{
    bool recording = (s_rec_state == AOS_REC_RECORDING);
    if (!recording && !s_mic_open) {
        s_rec_last_ms = aos_hal_uptime_ms();
        s_mic_level   = 0;
        s_mic_peak    = 0;
        return;
    }

    const int      samples  = (int)(s_mic_rate / AOS_REC_PEAK_HZ);
    const uint32_t block_ms = 1000 / AOS_REC_PEAK_HZ;
    int16_t buffer[4096];
    if (samples > (int)(sizeof(buffer) / sizeof(buffer[0]))) {
        return;
    }

    uint64_t now = aos_hal_uptime_ms();
    bool wrote = false;

    while (now - s_rec_last_ms >= block_ms) {
        s_rec_last_ms += block_ms;

        int peak = mic_synth_block(buffer, samples);
        s_mic_peak  = peak;
        s_mic_level = rec_level_from_peak(peak);

        s_rec_ring[s_rec_ring_w % SIM_REC_RING] = (uint8_t)s_mic_level;
        s_rec_ring_w++;

        if (s_mic_open) {
            pcm_push(buffer, samples);
        }
        if (recording && s_rec_file) {
            fwrite(buffer, sizeof(int16_t), (size_t)samples, s_rec_file);
            s_rec_bytes += (uint32_t)samples * (uint32_t)sizeof(int16_t);
            wrote = true;
        }
    }

    /* Just as on the board: the header is rewritten now and then so an abrupt
     * cut does not leave a WAV claiming to have zero audio. */
    if (wrote && s_rec_file && s_rec_bytes - s_rec_flushed >= s_rec_rate * 4) {
        s_rec_flushed = s_rec_bytes;
        rec_header_write(s_rec_file, s_rec_rate, s_rec_bytes);
        fseek(s_rec_file, 0, SEEK_END);
        fflush(s_rec_file);
    }
}

bool aos_hal_rec_start(const char *path, uint32_t sample_rate)
{
    if (!path || s_rec_state != AOS_REC_IDLE) {
        return false;
    }

    s_rec_file = fopen(path, "wb");
    if (!s_rec_file) {
        printf("[hal] could not create %s\n", path);
        return false;
    }

    snprintf(s_rec_path, sizeof(s_rec_path), "%s", path);
    /* If a capture is already running, the rate is its own: the same rule as
     * on the board, where the codec is already open and cannot be changed. */
    if (!s_mic_open) {
        s_mic_rate = sample_rate ? sample_rate : AOS_REC_RATE_HZ;
        s_rec_last_ms = aos_hal_uptime_ms();
    }
    s_rec_rate    = s_mic_rate;
    s_rec_bytes   = 0;
    s_rec_flushed = 0;
    s_rec_ring_w  = 0;
    s_rec_ring_r  = 0;
    s_syn_env     = 0.0f;
    s_syn_target  = 0.0f;
    s_syn_left    = 0;
    s_rec_state   = AOS_REC_RECORDING;

    rec_header_write(s_rec_file, s_rec_rate, 0);
    printf("[hal] recording into %s (%u Hz)\n", path, (unsigned)s_rec_rate);
    return true;
}

void aos_hal_rec_pause(void)
{
    mic_advance();
    if (s_rec_state == AOS_REC_RECORDING) {
        s_rec_state = AOS_REC_PAUSED;
    }
}

void aos_hal_rec_resume(void)
{
    if (s_rec_state == AOS_REC_PAUSED) {
        s_rec_last_ms = aos_hal_uptime_ms();
        s_rec_state   = AOS_REC_RECORDING;
    }
}

bool aos_hal_rec_stop(void)
{
    mic_advance();
    if (s_rec_file) {
        rec_header_write(s_rec_file, s_rec_rate, s_rec_bytes);
        fclose(s_rec_file);
        s_rec_file = NULL;
        printf("[hal] recording closed: %s (%u bytes)\n",
               s_rec_path, (unsigned)s_rec_bytes);
    }
    s_rec_state = AOS_REC_IDLE;
    return s_rec_bytes > 0;
}

bool aos_hal_rec_status(aos_rec_status_t *out)
{
    if (!out) {
        return false;
    }
    mic_advance();

    out->state       = s_rec_state;
    out->bytes       = s_rec_bytes;
    out->sample_rate = s_rec_rate;
    out->channels    = 1;
    out->level       = s_mic_level;
    out->elapsed_ms  = s_rec_rate
                     ? (uint32_t)((uint64_t)s_rec_bytes * 500 / s_rec_rate)
                     : 0;
    snprintf(out->path, sizeof(out->path), "%s", s_rec_path);
    return true;
}

int aos_hal_rec_peaks(uint8_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }
    mic_advance();

    uint32_t pending = s_rec_ring_w - s_rec_ring_r;
    if (pending > SIM_REC_RING) {
        s_rec_ring_r = s_rec_ring_w - SIM_REC_RING;
        pending = SIM_REC_RING;
    }
    if (pending > (uint32_t)max) {
        s_rec_ring_r = s_rec_ring_w - (uint32_t)max;
        pending = (uint32_t)max;
    }
    for (uint32_t i = 0; i < pending; i++) {
        out[i] = s_rec_ring[(s_rec_ring_r + i) % SIM_REC_RING];
    }
    s_rec_ring_r += pending;
    return (int)pending;
}

int  aos_hal_volume_get(void) { return s_volume; }
void aos_hal_volume_set(int percent) { s_volume = percent; }
int  aos_hal_mic_level(void) { mic_advance(); return rec_level(); }

/* -------------------------------------------------------------------------- */
/* Raw microphone                                                              */
/* -------------------------------------------------------------------------- */

bool aos_hal_mic_open(uint32_t sample_rate)
{
    if (s_mic_open) {
        return true;
    }
    uint32_t rate = (s_rec_state != AOS_REC_IDLE)
                  ? s_mic_rate
                  : (sample_rate ? sample_rate : AOS_MIC_RATE_HZ);

    if (!s_pcm_ring || s_pcm_len < rate) {
        int16_t *ring = malloc((size_t)rate * sizeof(int16_t));
        if (!ring) {
            return false;
        }
        free(s_pcm_ring);
        s_pcm_ring = ring;
        s_pcm_len  = rate;
    }

    s_pcm_w       = 0;
    s_pcm_r       = 0;
    s_pcm_dropped = 0;
    if (s_rec_state == AOS_REC_IDLE) {
        s_mic_rate    = rate;
        s_rec_last_ms = aos_hal_uptime_ms();
    }
    s_mic_open = true;
    printf("[hal] microphone open (%u Hz)\n", (unsigned)s_mic_rate);
    return true;
}

void aos_hal_mic_close(void)
{
    mic_advance();
    s_mic_open = false;
}

int aos_hal_mic_read(int16_t *out, int max)
{
    if (!out || max <= 0 || !s_pcm_ring || !s_pcm_len) {
        return 0;
    }
    mic_advance();

    uint32_t pending = s_pcm_w - s_pcm_r;
    if (pending > s_pcm_len) {
        s_pcm_dropped += pending - s_pcm_len;
        s_pcm_r = s_pcm_w - s_pcm_len;
        pending = s_pcm_len;
    }
    if (pending > (uint32_t)max) {
        pending = (uint32_t)max;
    }
    for (uint32_t i = 0; i < pending; i++) {
        out[i] = s_pcm_ring[(s_pcm_r + i) % s_pcm_len];
    }
    s_pcm_r += pending;
    return (int)pending;
}

int aos_hal_mic_available(void)
{
    if (!s_pcm_ring || !s_pcm_len) {
        return 0;
    }
    mic_advance();
    uint32_t pending = s_pcm_w - s_pcm_r;
    return (int)(pending > s_pcm_len ? s_pcm_len : pending);
}

bool aos_hal_mic_status(aos_mic_status_t *out)
{
    if (!out) {
        return false;
    }
    mic_advance();
    memset(out, 0, sizeof(*out));
    out->open        = s_mic_open || s_rec_state != AOS_REC_IDLE;
    out->sample_rate = s_mic_rate;
    out->gain_db     = s_mic_gain_db;
    out->level       = s_mic_level;
    out->peak        = s_mic_peak;
    out->dropped     = s_pcm_dropped;
    return true;
}

void aos_hal_mic_gain_set(int db)
{
    if (db < 0)  db = 0;
    if (db > 42) db = 42;
    s_mic_gain_db = (db + 3) / 6 * 6;
}

int aos_hal_mic_gain_get(void)
{
    return s_mic_gain_db;
}

/* -------------------------------------------------------------------------- */


/* -------------------------------------------------------------------------- */
/* Network survey (synthetic)                                                  */
/*                                                                             */
/* The simulator sweeps nothing for real: it invents a plausible survey and    */
/* writes the SAME NDJSON the board does, into sim_fs/redes. It is the same    */
/* pattern as rec_synth_block() for audio, and it serves the same purpose: to  */
/* design the app and the portal's /red page without depending on the hardware */
/* or on there being an interesting network around.                            */
/*                                                                             */
/* It advances by elapsed time each time somebody asks for the status, with no */
/* threads, just like the microphone capture.                                  */
/* -------------------------------------------------------------------------- */

#define SIM_WIFI_MS     1500
#define SIM_HOSTS_MS    4000
#define SIM_PORTS_MS    3000
#define SIM_MDNS_MS     2000

static const struct { const char *ssid; int rssi, canal; const char *cif; } SIM_REDES[] = {
    { "casa-fibra",      -42, 6,  "WPA2" },
    { "casa-fibra-5G",   -55, 44, "WPA2/WPA3" },
    { "Fibertel WiFi",   -71, 1,  "WPA2" },
    { "vecino_2.4",      -78, 11, "WPA2" },
    { "",                -80, 3,  "WPA2" },
    { "Invitados",       -66, 6,  "abierta" },
};
#define SIM_N_REDES ((int)(sizeof(SIM_REDES) / sizeof(SIM_REDES[0])))

/* Synthetic mDNS names, so the part of the page that shows them can be
 * designed without depending on there being an Apple TV switched on next to
 * you. */
static const struct { int ultimo; const char *nombre; const char *serv; int puerto; } SIM_NOMBRES[] = {
    {   1, "router",          "_http._tcp",     80 },
    {  17, "homeassistant",   "_http._tcp",     8123 },
    {  23, "nas",             "_smb._tcp",      445 },
    {  45, "HP LaserJet",     "_ipp._tcp",      631 },
    { 102, "camara-living",   "_rtsp._tcp",     554 },
};
#define SIM_N_NOMBRES ((int)(sizeof(SIM_NOMBRES) / sizeof(SIM_NOMBRES[0])))

static const struct { int ultimo; const char *mac; int ping; const char *puertos; } SIM_HOSTS[] = {
    {   1, "a4:2b:8c:11:02:5f", 1, "53,80,443"      },   /* the router   */
    {  17, "b8:27:eb:9a:44:01", 1, "22,80,1883,8123"},   /* Home Assistant*/
    {  23, "00:11:32:aa:bc:10", 1, "22,80,445,5000" },   /* NAS          */
    {  45, "3c:2e:ff:07:19:88", 0, "631,9100"       },   /* printer      */
    {  60, "f0:18:98:3d:20:71", 1, ""               },   /* phone        */
    { 102, "dc:a6:32:0e:55:c3", 0, "554,80"         },   /* camera       */
    { 133, "8c:85:90:12:76:aa", 1, ""               },   /* laptop       */
};
#define SIM_N_HOSTS ((int)(sizeof(SIM_HOSTS) / sizeof(SIM_HOSTS[0])))

static aos_scan_phase_t s_scan_phase;
static uint32_t s_scan_flags;
static uint64_t s_scan_inicio;
static char     s_scan_path[160];
static FILE    *s_scan_file;
static int      s_scan_wifi, s_scan_hosts, s_scan_ports;
static int      s_scan_done, s_scan_total;
static int      s_scan_escritos_wifi, s_scan_escritos_host, s_scan_escritos_port;
static int      s_scan_escritos_nombre;
static bool     s_scan_fin_escrito;

static void scan_advance(void)
{
    if (s_scan_phase == AOS_SCAN_IDLE || s_scan_phase == AOS_SCAN_DONE ||
        s_scan_phase == AOS_SCAN_FAILED) {
        return;
    }

    uint64_t t = aos_hal_uptime_ms() - s_scan_inicio;
    uint64_t fin_wifi  = (s_scan_flags & AOS_SCAN_WIFI)  ? SIM_WIFI_MS : 0;
    uint64_t fin_hosts = fin_wifi + ((s_scan_flags & AOS_SCAN_HOSTS) ? SIM_HOSTS_MS : 0);
    uint64_t fin_ports = fin_hosts + ((s_scan_flags & AOS_SCAN_PORTS) ? SIM_PORTS_MS : 0);
    uint64_t fin_mdns  = fin_ports + ((s_scan_flags & AOS_SCAN_MDNS) ? SIM_MDNS_MS : 0);

    if (t < fin_wifi) {
        s_scan_phase = AOS_SCAN_PH_WIFI;
        s_scan_total = SIM_N_REDES;
        s_scan_done  = (int)(t * SIM_N_REDES / (fin_wifi ? fin_wifi : 1));
    } else if (t < fin_hosts) {
        s_scan_phase = AOS_SCAN_PH_HOSTS;
        s_scan_total = 254;
        s_scan_done  = (int)((t - fin_wifi) * 254 / SIM_HOSTS_MS);
    } else if (t < fin_ports) {
        s_scan_phase = AOS_SCAN_PH_PORTS;
        s_scan_total = SIM_N_HOSTS * 18;
        s_scan_done  = (int)((t - fin_hosts) * s_scan_total / SIM_PORTS_MS);
    } else if (t < fin_mdns) {
        s_scan_phase = AOS_SCAN_PH_MDNS;
        s_scan_total = SIM_N_NOMBRES;
        s_scan_done  = (int)((t - fin_ports) * SIM_N_NOMBRES / SIM_MDNS_MS);
    } else {
        s_scan_phase = AOS_SCAN_DONE;
        s_scan_done  = s_scan_total;
    }

    /* Writing down what "is being found" as it goes, just like the board. */
    while ((s_scan_flags & AOS_SCAN_WIFI) && s_scan_escritos_wifi < SIM_N_REDES &&
           (t * SIM_N_REDES / (fin_wifi ? fin_wifi : 1)) > (uint64_t)s_scan_escritos_wifi) {
        int i = s_scan_escritos_wifi++;
        if (s_scan_file) {
            fprintf(s_scan_file,
                    "{\"t\":\"wifi\",\"ssid\":\"%s\",\"bssid\":\"02:00:%02x:%02x:%02x:%02x\","
                    "\"rssi\":%d,\"canal\":%d,\"cifrado\":\"%s\",\"oculta\":%s}\n",
                    SIM_REDES[i].ssid, i, i * 7 + 3, i * 13 + 9, i * 31 + 5,
                    SIM_REDES[i].rssi, SIM_REDES[i].canal, SIM_REDES[i].cif,
                    SIM_REDES[i].ssid[0] ? "false" : "true");
            fflush(s_scan_file);
        }
        s_scan_wifi = s_scan_escritos_wifi;
    }

    if (t >= fin_wifi && (s_scan_flags & AOS_SCAN_HOSTS)) {
        uint64_t avance = (t > fin_hosts) ? SIM_HOSTS_MS : (t - fin_wifi);
        while (s_scan_escritos_host < SIM_N_HOSTS &&
               avance * SIM_N_HOSTS / SIM_HOSTS_MS > (uint64_t)s_scan_escritos_host) {
            int i = s_scan_escritos_host++;
            if (s_scan_file) {
                fprintf(s_scan_file,
                        "{\"t\":\"host\",\"ip\":\"192.168.1.%d\",\"mac\":\"%s\","
                        "\"ping\":%s}\n",
                        SIM_HOSTS[i].ultimo, SIM_HOSTS[i].mac,
                        SIM_HOSTS[i].ping ? "true" : "false");
                fflush(s_scan_file);
            }
            s_scan_hosts = s_scan_escritos_host;
        }
    }

    if (t >= fin_hosts && (s_scan_flags & AOS_SCAN_PORTS)) {
        uint64_t avance = (t > fin_ports) ? SIM_PORTS_MS : (t - fin_hosts);
        while (s_scan_escritos_port < SIM_N_HOSTS &&
               avance * SIM_N_HOSTS / SIM_PORTS_MS > (uint64_t)s_scan_escritos_port) {
            int i = s_scan_escritos_port++;
            if (!SIM_HOSTS[i].puertos[0]) {
                continue;
            }
            if (s_scan_file) {
                fprintf(s_scan_file,
                        "{\"t\":\"puertos\",\"ip\":\"192.168.1.%d\",\"abiertos\":[%s]}\n",
                        SIM_HOSTS[i].ultimo, SIM_HOSTS[i].puertos);
                fflush(s_scan_file);
            }
            for (const char *c = SIM_HOSTS[i].puertos; *c; c++) {
                if (*c == ',') s_scan_ports++;
            }
            s_scan_ports++;
        }
    }

    if (t >= fin_ports && (s_scan_flags & AOS_SCAN_MDNS)) {
        uint64_t avance = (t > fin_mdns) ? SIM_MDNS_MS : (t - fin_ports);
        while (s_scan_escritos_nombre < SIM_N_NOMBRES &&
               avance * SIM_N_NOMBRES / SIM_MDNS_MS > (uint64_t)s_scan_escritos_nombre) {
            int i = s_scan_escritos_nombre++;
            if (s_scan_file) {
                fprintf(s_scan_file,
                        "{\"t\":\"nombre\",\"ip\":\"192.168.1.%d\",\"nombre\":\"%s\","
                        "\"host\":\"%s.local\",\"servicio\":\"%s\",\"puerto\":%d}\n",
                        SIM_NOMBRES[i].ultimo, SIM_NOMBRES[i].nombre,
                        SIM_NOMBRES[i].nombre, SIM_NOMBRES[i].serv,
                        SIM_NOMBRES[i].puerto);
                fflush(s_scan_file);
            }
        }
    }

    if (s_scan_phase == AOS_SCAN_DONE && !s_scan_fin_escrito) {
        s_scan_fin_escrito = true;
        if (s_scan_file) {
            fprintf(s_scan_file,
                    "{\"t\":\"fin\",\"redes\":%d,\"equipos\":%d,\"puertos\":%d,"
                    "\"ms\":%u,\"cortado\":false}\n",
                    s_scan_wifi, s_scan_hosts, s_scan_ports, (unsigned)t);
            fclose(s_scan_file);
            s_scan_file = NULL;
        }
        printf("[hal] synthetic scan finished: %s\n", s_scan_path);
    }
}

bool aos_hal_scan_start(uint32_t flags)
{
    if (s_scan_phase == AOS_SCAN_PH_WIFI || s_scan_phase == AOS_SCAN_PH_HOSTS ||
        s_scan_phase == AOS_SCAN_PH_PORTS) {
        return false;
    }
    if (!flags) {
        flags = AOS_SCAN_TODO;
    }

    mkdir("sim_fs/redes", 0755);
    time_t ahora = time(NULL);
    struct tm t;
    localtime_r(&ahora, &t);
    snprintf(s_scan_path, sizeof(s_scan_path), "%s/%04d%02d%02d-%02d%02d.ndjson",
             aos_hal_path_scans(), t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min);

    s_scan_file = fopen(s_scan_path, "w");
    if (s_scan_file) {
        fprintf(s_scan_file,
                "{\"t\":\"inicio\",\"fecha\":\"%04d-%02d-%02d %02d:%02d\","
                "\"ssid\":\"simulator\",\"ip\":\"192.168.1.50\","
                "\"mascara\":\"255.255.255.0\",\"rssi\":-54}\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
        fflush(s_scan_file);
    }

    s_scan_flags  = flags;
    s_scan_inicio = aos_hal_uptime_ms();
    s_scan_phase  = AOS_SCAN_PH_WIFI;
    s_scan_wifi = s_scan_hosts = s_scan_ports = 0;
    s_scan_done = s_scan_total = 0;
    s_scan_escritos_wifi = s_scan_escritos_host = s_scan_escritos_port = 0;
    s_scan_escritos_nombre = 0;
    s_scan_fin_escrito = false;
    printf("[hal] synthetic scan into %s\n", s_scan_path);
    return true;
}

void aos_hal_scan_stop(void)
{
    scan_advance();
    if (s_scan_file) {
        fprintf(s_scan_file,
                "{\"t\":\"fin\",\"redes\":%d,\"equipos\":%d,\"puertos\":%d,"
                "\"ms\":%u,\"cortado\":true}\n",
                s_scan_wifi, s_scan_hosts, s_scan_ports,
                (unsigned)(aos_hal_uptime_ms() - s_scan_inicio));
        fclose(s_scan_file);
        s_scan_file = NULL;
    }
    s_scan_phase = AOS_SCAN_FAILED;
}

bool aos_hal_scan_status(aos_scan_status_t *out)
{
    if (!out) {
        return false;
    }
    scan_advance();
    memset(out, 0, sizeof(*out));
    out->phase       = s_scan_phase;
    out->wifi_found  = s_scan_wifi;
    out->hosts_found = s_scan_hosts;
    out->ports_found = s_scan_ports;
    out->done        = s_scan_done;
    out->total       = s_scan_total;
    out->elapsed_ms  = s_scan_inicio
                     ? (uint32_t)(aos_hal_uptime_ms() - s_scan_inicio) : 0;
    snprintf(out->path, sizeof(out->path), "%s", s_scan_path);
    return true;
}

aos_net_state_t aos_hal_net_state(void) { return AOS_NET_CONNECTED; }
const char *aos_hal_net_ssid(void)      { return "simulator"; }
int         aos_hal_net_rssi(void)      { return -54; }
const char *aos_hal_net_ip(void)        { return "127.0.0.1"; }
bool        aos_hal_net_sync_time(void) { return true; }

/* Network onboarding of 27/08: the firmware gained stored credentials and a
 * setup AP. Here faking it is enough — the simulator is always "at home" and
 * connected — but it has to exist, or aos_app_settings does not link. */
static bool s_net_on = true;
static bool s_net_creds = true;
static bool s_net_ap;

void        aos_hal_net_enable(bool on) { s_net_on = on; }
bool        aos_hal_net_enabled(void)   { return s_net_on; }
bool        aos_hal_net_has_credentials(void) { return s_net_creds; }
void        aos_hal_net_forget(void)    { s_net_creds = false; }

/* The AP's name and password ARE really simulated, with the real preferences:
 * it is the only way to test the two password modes and the QR that shows them
 * on the Mac. What is not simulated is the radio. */
#define SIM_AP_KEY_SSID  "ap_ssid"
#define SIM_AP_KEY_PASS  "ap_pass"
#define SIM_AP_KEY_MODE  "ap_pmode"
#define SIM_AP_PASS      "amoledos"

static char s_ap_ssid[33];
static char s_ap_pass[65];

const char *aos_hal_net_ap_default_ssid(void) { return "AmoledOS-5IM"; }

aos_ap_pass_mode_t aos_hal_net_ap_pass_mode(void)
{
    int32_t modo = AOS_AP_PASS_FIXED;
    aos_hal_pref_get_i32(SIM_AP_KEY_MODE, &modo);
    return modo == AOS_AP_PASS_ROTATING ? AOS_AP_PASS_ROTATING
                                        : AOS_AP_PASS_FIXED;
}

/* The same alphabet as the board: no 0/O and no 1/l/I, and none of the
 * characters that have to be escaped inside the QR's text. */
static const char SIM_AP_ALFABETO[] =
    "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";

static void ap_config_resolver(bool rotar)
{
    char guardado[33] = {0};
    if (aos_hal_pref_get_str(SIM_AP_KEY_SSID, guardado, sizeof(guardado)) &&
        guardado[0]) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", guardado);
    } else {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", aos_hal_net_ap_default_ssid());
    }

    char clave[65] = {0};
    bool hay = aos_hal_pref_get_str(SIM_AP_KEY_PASS, clave, sizeof(clave)) &&
               clave[0];

    if (aos_hal_net_ap_pass_mode() == AOS_AP_PASS_ROTATING) {
        if (rotar || !hay) {
            char nueva[11] = {0};
            for (int i = 0; i < 10; i++) {
                nueva[i] = SIM_AP_ALFABETO[rand() % (int)(sizeof(SIM_AP_ALFABETO) - 1)];
            }
            snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", nueva);
            aos_hal_pref_set_str(SIM_AP_KEY_PASS, s_ap_pass);
        } else {
            snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", clave);
        }
        return;
    }

    snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", hay ? clave : SIM_AP_PASS);
}

bool aos_hal_net_ap_start(void)
{
    ap_config_resolver(true);
    s_net_ap = true;
    return true;
}

void        aos_hal_net_ap_stop(void)   { s_net_ap = false; }

/* AOS_SIM_AP=1 starts with the access point up. On the board you have to tap
 * "Configurar red" first, and that leaves the AP screen -the one with the
 * password and the QR- two taps and a scroll away: too fragile for a script.
 * With this the touchable row is there from startup. */
bool aos_hal_net_ap_active(void)
{
    static int forzado = -1;
    if (forzado < 0) {
        const char *env = getenv("AOS_SIM_AP");
        forzado = (env && *env && *env != '0') ? 1 : 0;
        if (forzado) {
            aos_hal_net_ap_start();
        }
    }
    return s_net_ap;
}
const char *aos_hal_net_ap_ip(void)     { return "192.168.4.1"; }

const char *aos_hal_net_ap_ssid(void)
{
    if (!s_net_ap) {
        ap_config_resolver(false);
    }
    return s_ap_ssid;
}

const char *aos_hal_net_ap_pass(void)
{
    if (!s_net_ap) {
        ap_config_resolver(false);
    }
    return s_ap_pass;
}

bool aos_hal_net_ap_set_config(const char *ssid, const char *pass,
                               aos_ap_pass_mode_t mode)
{
    if (ssid && ssid[0] && strlen(ssid) > 32) {
        return false;
    }
    if (mode == AOS_AP_PASS_FIXED && pass && pass[0] &&
        (strlen(pass) < 8 || strlen(pass) > 63)) {
        return false;
    }
    aos_hal_pref_set_str(SIM_AP_KEY_SSID, ssid ? ssid : "");
    aos_hal_pref_set_i32(SIM_AP_KEY_MODE, (int32_t)mode);
    aos_hal_pref_set_str(SIM_AP_KEY_PASS,
                         (mode == AOS_AP_PASS_FIXED && pass) ? pass : "");
    s_ap_pass[0] = 0;
    ap_config_resolver(mode == AOS_AP_PASS_ROTATING);
    return true;
}

/* -------------------------------------------------------------------------- */

void aos_hal_heap_info(uint32_t *free_internal, uint32_t *free_psram)
{
    if (free_internal) *free_internal = 240 * 1024;
    if (free_psram)    *free_psram    = 6 * 1024 * 1024;
}

const char *aos_hal_board_name(void)       { return "SDL simulator"; }
const char *aos_hal_firmware_version(void) { return "0.1.0-dev"; }

void aos_hal_log(const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("[%s] ", tag);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
