#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include <stdint.h>

#define AUDIO_BUFFER_SIZE 2048 /* Total buffer size in int16_t samples */

/* Global audio buffer used by DMA */
extern int16_t audio_buffer[AUDIO_BUFFER_SIZE];

/**
 * @brief Initialize sine wave generator
 */
void AudioSynth_Init(void);

/**
 * @brief Fill buffer with sine wave samples
 * @param buffer Buffer to fill
 * @param num_samples Number of indices to fill (should be even)
 */
void AudioSynth_FillBuffer(int16_t *buffer, uint16_t num_samples);

#endif
