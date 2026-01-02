#ifndef SEQUENCER_H
#define SEQUENCER_H

#include <stdint.h>

#define NUM_CHANNELS 6
#define MAX_STEPS 32

/**
 * @brief Pattern structure
 */
typedef struct {
  uint8_t steps[NUM_CHANNELS][MAX_STEPS]; /* 0=off, 1-255=velocity */
  uint8_t step_count;                     /* Active steps (1-32) */
  uint16_t bpm;                           /* Beats per minute */
  char name[16];                          /* Pattern name */
} Pattern;

/**
 * @brief Initialize sequencer
 */
void Sequencer_Init(void);

/**
 * @brief Start sequencer playback
 */
void Sequencer_Start(void);

/**
 * @brief Stop sequencer playback
 */
void Sequencer_Stop(void);

/**
 * @brief Check if sequencer is playing
 * @return 1 if playing, 0 if stopped
 */
uint8_t Sequencer_IsPlaying(void);

/**
 * @brief Get current step
 * @return Current step (0-31)
 */
uint8_t Sequencer_GetCurrentStep(void);

/**
 * @brief Get current pattern
 * @return Pointer to current pattern
 */
Pattern *Sequencer_GetPattern(void);

/**
 * @brief Set step value
 * @param channel Channel (0-3)
 * @param step Step (0-31)
 * @param value Velocity (0=off, 1-255=on with velocity)
 */
void Sequencer_SetStep(uint8_t channel, uint8_t step, uint8_t value);

/**
 * @brief Get step value
 * @param channel Channel (0-3)
 * @param step Step (0-31)
 * @return Velocity value
 */
uint8_t Sequencer_GetStep(uint8_t channel, uint8_t step);

/**
 * @brief Toggle step (on/off)
 * @param channel Channel (0-3)
 * @param step Step (0-31)
 */
void Sequencer_ToggleStep(uint8_t channel, uint8_t step);

/**
 * @brief Set BPM
 * @param bpm Beats per minute (40-300)
 */
void Sequencer_SetBPM(uint16_t bpm);

/**
 * @brief Get BPM
 * @return Current BPM
 */
uint16_t Sequencer_GetBPM(void);

/**
 * @brief Set step count
 * @param count Number of steps (1-32)
 */
void Sequencer_SetStepCount(uint8_t count);

/**
 * @brief Get step count
 * @return Number of active steps
 */
uint8_t Sequencer_GetStepCount(void);

/**
 * @brief Clear pattern
 */
void Sequencer_ClearPattern(void);

#endif
