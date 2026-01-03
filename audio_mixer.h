#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdint.h>

#define NUM_CHANNELS 6

/**
 * @brief Initialize audio mixer
 */
void AudioMixer_Init(void);

/**
 * @brief Set sample for channel
 * @param channel Channel number (0-3)
 * @param sample_data Pointer to sample data
 * @param sample_length Length in samples
 */
void AudioMixer_SetSample(uint8_t channel, int16_t *sample_data,
                          uint32_t sample_length);

/**
 * @brief Set pan for channel
 * @param channel Channel number (0-3)
 * @param pan Pan value (0=Left, 128=Center, 255=Right)
 */
void AudioMixer_SetPan(uint8_t channel, uint8_t pan);

/**
 * @brief Set volume (mix level) for channel
 * @param channel Channel number (0-5)
 * @param volume Volume level (0-255)
 */
void AudioMixer_SetVolume(uint8_t channel, uint8_t volume);

/**
 * @brief Trigger sample on channel
 * @param channel Channel number (0-3)
 * @param velocity Velocity (0-255)
 */
void AudioMixer_Trigger(uint8_t channel, uint8_t velocity);

/**
 * @brief Process audio (fill output buffer)
 * @param output Output buffer (stereo interleaved)
 * @param length Number of stereo frames
 */
void AudioMixer_Process(int16_t *output, uint32_t length);

#endif
