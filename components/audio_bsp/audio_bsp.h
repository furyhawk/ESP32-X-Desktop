#pragma once

#include <esp_err.h>

/**
 * Play a short welcome chime during desktop power-up.
 * Returns ESP_OK when played, ESP_ERR_NOT_SUPPORTED when audio is unavailable,
 * or another error when initialization/playback fails.
 */
esp_err_t BoardAudio_PlayWelcomeSound(void);
