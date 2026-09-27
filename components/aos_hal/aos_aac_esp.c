/*
 * AmoledOS - AAC on the board: esp_audio_codec. See aos_aac.h.
 *
 * AAC-Plus (SBR, and PS for v2) is switched on: most AAC radio is HE-AAC
 * at 32-64 kbps, and without it those stations decode at half the sample
 * rate, dull and in mono. Espressif gives 6.75 % of a core and 51 KB for
 * AAC-LC 48 kHz stereo on the S3; what HE-AAC costs here is in
 * docs/RADIO.md. Its buffers are malloc()s above the PSRAM threshold, so
 * they land in PSRAM.
 */
#include "aos_aac.h"

#include <stdlib.h>
#include <string.h>

#include "decoder/impl/esp_aac_dec.h"

struct aos_aac {
    void *h;
};

aos_aac_t *aos_aac_open(void)
{
    aos_aac_t *d = calloc(1, sizeof(*d));
    if (!d) {
        return NULL;
    }
    esp_aac_dec_cfg_t cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
    cfg.aac_plus_enable = true;
    if (esp_aac_dec_open(&cfg, sizeof(cfg), &d->h) != ESP_AUDIO_ERR_OK || !d->h) {
        free(d);
        return NULL;
    }
    return d;
}

int aos_aac_decode(aos_aac_t *d, const uint8_t *frame, int len, int16_t *pcm,
                   uint32_t *rate, uint8_t *channels)
{
    esp_audio_dec_in_raw_t raw = { .buffer = (uint8_t *)frame, .len = (uint32_t)len };
    esp_audio_dec_out_frame_t out = { .buffer = (uint8_t *)pcm,
                                      .len = AOS_AAC_MAX_SAMPLES * sizeof(int16_t) };
    esp_audio_dec_info_t info;
    memset(&info, 0, sizeof(info));
    esp_audio_err_t err = esp_aac_dec_decode(d->h, &raw, &out, &info);
    if (err == ESP_AUDIO_ERR_DATA_LACK || err == ESP_AUDIO_ERR_CONTINUE) {
        return 0;
    }
    if (err != ESP_AUDIO_ERR_OK || !info.channel || !info.sample_rate) {
        return -1;
    }
    *rate = info.sample_rate;
    *channels = info.channel;
    return (int)(out.decoded_size / (sizeof(int16_t) * info.channel));
}

void aos_aac_close(aos_aac_t *d)
{
    if (d) {
        esp_aac_dec_close(d->h);
        free(d);
    }
}
