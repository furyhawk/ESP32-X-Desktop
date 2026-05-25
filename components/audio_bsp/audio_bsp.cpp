#include "audio_bsp.h"

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "hw_platform.h"

#if PLATFORM_HAS_AUDIO
#include "codec_board.h"
#include "codec_init.h"
#include "esp_codec_dev.h"
#endif

#define TAG "audio_bsp"

#if PLATFORM_HAS_AUDIO
static esp_err_t board_audio_play_square_tone(esp_codec_dev_handle_t speaker,
                                              uint32_t freq_hz,
                                              uint32_t duration_ms,
                                              uint32_t sample_rate_hz)
{
    if(speaker == NULL || freq_hz == 0 || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    static constexpr uint32_t kChunkFrames = 256;
    static constexpr int16_t kAmplitude = 5000;
    int16_t pcm[kChunkFrames * 2] = {};

    uint32_t frames_total = (sample_rate_hz * duration_ms) / 1000U;
    if(frames_total == 0) {
        return ESP_OK;
    }

    uint32_t half_period = sample_rate_hz / (freq_hz * 2U);
    if(half_period == 0) {
        half_period = 1;
    }

    uint32_t frame_index = 0;
    while(frame_index < frames_total) {
        uint32_t frames_now = frames_total - frame_index;
        if(frames_now > kChunkFrames) {
            frames_now = kChunkFrames;
        }

        for(uint32_t i = 0; i < frames_now; ++i) {
            uint32_t phase_block = (frame_index + i) / half_period;
            int16_t sample = (phase_block & 1U) ? kAmplitude : (int16_t)-kAmplitude;
            pcm[(i * 2U)] = sample;
            pcm[(i * 2U) + 1U] = sample;
        }

        int write_ret = esp_codec_dev_write(speaker, pcm, frames_now * sizeof(int16_t) * 2U);
        if(write_ret != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }

        frame_index += frames_now;
    }

    return ESP_OK;
}
#endif

esp_err_t BoardAudio_PlayWelcomeSound(void)
{
#if !PLATFORM_HAS_AUDIO
    return ESP_ERR_NOT_SUPPORTED;
#else
    set_codec_board_type("C6_AMOLED_2_16");

    codec_init_cfg_t codec_cfg = {};
    codec_cfg.in_mode = CODEC_I2S_MODE_NONE;
    codec_cfg.out_mode = CODEC_I2S_MODE_TDM;
    codec_cfg.in_use_tdm = false;
    codec_cfg.reuse_dev = false;

    if(init_codec(&codec_cfg) != 0) {
        ESP_LOGW(TAG, "Audio codec init failed; skipping welcome sound");
        return ESP_FAIL;
    }

    esp_codec_dev_handle_t speaker = get_playback_handle();
    if(speaker == NULL) {
        ESP_LOGW(TAG, "No speaker handle available; skipping welcome sound");
        deinit_codec();
        return ESP_ERR_NOT_FOUND;
    }

    esp_codec_dev_sample_info_t sample_info = {};
    sample_info.sample_rate = 16000;
    sample_info.channel = 2;
    sample_info.bits_per_sample = 16;

    if(esp_codec_dev_open(speaker, &sample_info) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to open speaker; skipping welcome sound");
        deinit_codec();
        return ESP_FAIL;
    }

    esp_codec_dev_set_out_vol(speaker, 75);

    esp_err_t tone_ret = ESP_OK;
    if(board_audio_play_square_tone(speaker, 880, 90, sample_info.sample_rate) != ESP_OK) {
        tone_ret = ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if(board_audio_play_square_tone(speaker, 1175, 90, sample_info.sample_rate) != ESP_OK) {
        tone_ret = ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if(board_audio_play_square_tone(speaker, 1568, 140, sample_info.sample_rate) != ESP_OK) {
        tone_ret = ESP_FAIL;
    }

    esp_codec_dev_close(speaker);
    deinit_codec();

    return tone_ret;
#endif
}
