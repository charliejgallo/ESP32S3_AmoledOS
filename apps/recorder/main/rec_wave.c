/*
 * Recorder - waveform.
 */
#include "rec_wave.h"

#define WAVE_MAX_BARS   64
#define WAVE_MIN_H      3       /* the silence line, so it does not disappear */

struct rec_wave_s {
    lv_obj_t  *root;
    lv_obj_t  *bar[WAVE_MAX_BARS];
    uint8_t    peak[WAVE_MAX_BARS];
    int        count;
    int32_t    height;
    int32_t    pitch;       /* spacing between bars, in px */
    lv_color_t color;
    int        progress;    /* -1 = no cursor */
};

/* Geometry only: height and vertical position.
 *
 * This runs 40 times per frame, so it touches no styles. Writing colour and
 * opacity into every bar on every frame is very expensive — each setter marks
 * the object for a style refresh as well as invalidating it — and they are not
 * needed anyway: the colour changes when it is paused and the opacity when the
 * playback cursor advances, not twenty times a second.
 *
 * lv_obj_align() is not used either: x is set once when the bar is created and
 * here only y is moved, so it grows from the axis in both directions. */
static void bar_geometry(rec_wave_t *wave, int index)
{
    int32_t full = wave->height;
    int32_t size = WAVE_MIN_H + (full - WAVE_MIN_H) * wave->peak[index] / 100;

    lv_obj_set_height(wave->bar[index], size);
    lv_obj_set_y(wave->bar[index], (full - size) / 2);
}

static void bar_style(rec_wave_t *wave, int index)
{
    bool lit = wave->progress < 0 || index <= wave->progress;
    lv_obj_set_style_bg_color(wave->bar[index], wave->color, 0);
    lv_obj_set_style_bg_opa(wave->bar[index], lit ? LV_OPA_COVER : LV_OPA_30, 0);
}

rec_wave_t *rec_wave_create(lv_obj_t *parent, int32_t width, int32_t height,
                            int bars, lv_color_t color)
{
    if (bars > WAVE_MAX_BARS) {
        bars = WAVE_MAX_BARS;
    }

    rec_wave_t *wave = lv_malloc_zeroed(sizeof(rec_wave_t));
    if (!wave) {
        return NULL;
    }
    wave->count    = bars;
    wave->height   = height;
    wave->color    = color;
    wave->progress = -1;

    wave->root = lv_obj_create(parent);
    lv_obj_remove_style_all(wave->root);
    lv_obj_set_size(wave->root, width, height);
    lv_obj_remove_flag(wave->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(wave->root, LV_OBJ_FLAG_CLICKABLE);

    /* The bar width comes from an integer split so the last one ends exactly
     * at the edge: splitting with decimals leaves a visible gap. */
    int32_t pitch = width / bars;
    int32_t bar_w = pitch - 2 > 2 ? pitch - 2 : 2;
    wave->pitch = pitch;

    for (int i = 0; i < bars; i++) {
        lv_obj_t *bar = lv_obj_create(wave->root);
        lv_obj_remove_style_all(bar);
        lv_obj_set_width(bar, bar_w);
        lv_obj_set_style_radius(bar, bar_w / 2, 0);
        lv_obj_set_style_bg_color(bar, color, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_x(bar, i * pitch);
        wave->bar[i] = bar;
        bar_geometry(wave, i);
        bar_style(wave, i);
    }
    return wave;
}

void rec_wave_delete(rec_wave_t *wave)
{
    lv_free(wave);
}

lv_obj_t *rec_wave_obj(rec_wave_t *wave)
{
    return wave ? wave->root : NULL;
}

int rec_wave_count(rec_wave_t *wave)
{
    return wave ? wave->count : 0;
}

void rec_wave_push(rec_wave_t *wave, uint8_t peak)
{
    rec_wave_push_many(wave, &peak, 1);
}

/* Several samples at once.
 *
 * It matters that it is a single pass: the app drains from the HAL everything
 * it has gathered since the previous frame, and pushing them one at a time
 * redrew all 40 bars per sample. With the drawing under load that feeds back —
 * the longer a frame takes, the more samples have to be pushed, and the longer
 * the next one takes. Here the array is shifted N places and redrawn once. */
void rec_wave_push_many(rec_wave_t *wave, const uint8_t *peaks, int count)
{
    if (!wave || !peaks || count <= 0) {
        return;
    }

    int bars = wave->count;
    if (count >= bars) {
        /* more than a whole screenful arrived: only the last ones matter */
        peaks += count - bars;
        count = bars;
    }

    for (int i = 0; i < bars - count; i++) {
        wave->peak[i] = wave->peak[i + count];
    }
    for (int i = 0; i < count; i++) {
        uint8_t value = peaks[i];
        wave->peak[bars - count + i] = value > 100 ? 100 : value;
    }

    for (int i = 0; i < bars; i++) {
        bar_geometry(wave, i);
    }
}

void rec_wave_set(rec_wave_t *wave, const uint8_t *peaks, int count)
{
    if (!wave || !peaks) {
        return;
    }
    for (int i = 0; i < wave->count; i++) {
        uint8_t value = i < count ? peaks[i] : 0;
        wave->peak[i] = value > 100 ? 100 : value;
        bar_geometry(wave, i);
    }
}

void rec_wave_clear(rec_wave_t *wave)
{
    if (!wave) {
        return;
    }
    for (int i = 0; i < wave->count; i++) {
        wave->peak[i] = 0;
        bar_geometry(wave, i);
    }
}

void rec_wave_set_color(rec_wave_t *wave, lv_color_t color)
{
    if (!wave) {
        return;
    }
    wave->color = color;
    for (int i = 0; i < wave->count; i++) {
        bar_style(wave, i);
    }
}

void rec_wave_set_progress(rec_wave_t *wave, int bars)
{
    if (!wave || wave->progress == bars) {
        return;
    }
    wave->progress = bars;
    for (int i = 0; i < wave->count; i++) {
        bar_style(wave, i);
    }
}
