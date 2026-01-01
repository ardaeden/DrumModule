#ifndef SEQUENCER_CLOCK_H
#define SEQUENCER_CLOCK_H

#include <stdint.h>

/**
 * @brief Initialize sequencer clock
 * @details Configures TIM2 for 24 PPQN (Pulses Per Quarter Note)
 *          Default: 120 BPM
 */
void Clock_Init(void);

/**
 * @brief Set BPM (Beats Per Minute)
 * @param bpm BPM value (40-300)
 */
void Clock_SetBPM(uint16_t bpm);

/**
 * @brief Get current BPM
 * @return Current BPM value
 */
uint16_t Clock_GetBPM(void);

/**
 * @brief Start the clock
 */
void Clock_Start(void);

/**
 * @brief Stop the clock
 */
void Clock_Stop(void);

/**
 * @brief Check if clock is running
 * @return 1 if running, 0 if stopped
 */
uint8_t Clock_IsRunning(void);

/**
 * @brief Get pulse count (0-23 for 24 PPQN)
 * @return Current pulse within quarter note
 */
uint8_t Clock_GetPulse(void);

/**
 * @brief Clock callback function type
 * @param pulse Current pulse (0-23)
 */
typedef void (*ClockCallback)(uint8_t pulse);

/**
 * @brief Set clock callback function
 * @param callback Function to call on each clock pulse
 */
void Clock_SetCallback(ClockCallback callback);

#endif
