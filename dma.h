#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/* Audio buffer for DMA transfers */
#define AUDIO_BUFFER_SIZE 4096 /* Stereo samples (2048 frames) */
extern int16_t audio_buffer[AUDIO_BUFFER_SIZE];

void DMA_Init_I2S(int16_t *buffer, uint32_t len);

#endif
