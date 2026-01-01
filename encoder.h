#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

/**
 * @brief Initialize rotary encoder
 * @details Configures PB6 (A), PB7 (B) with EXTI interrupts
 *          and PB8 (Button) as GPIO input
 */
void Encoder_Init(void);

/**
 * @brief Get current encoder value
 * @return Current encoder position
 */
int32_t Encoder_GetValue(void);

/**
 * @brief Set encoder value
 * @param value New encoder value
 */
void Encoder_SetValue(int32_t value);

/**
 * @brief Set encoder limits
 * @param min Minimum value
 * @param max Maximum value
 */
void Encoder_SetLimits(int32_t min, int32_t max);

/**
 * @brief Check if button is pressed
 * @return 1 if pressed, 0 if not
 */
uint8_t Encoder_ButtonPressed(void);

/**
 * @brief Check if button was clicked (press and release)
 * @return 1 if clicked, 0 if not (clears flag on read)
 */
uint8_t Encoder_ButtonClicked(void);

/**
 * @brief Get current increment step (1 or 10)
 * @return Current increment step value
 */
int32_t Encoder_GetIncrementStep(void);

/**
 * @brief Update button state (call from main loop)
 */
void Encoder_UpdateButton(void);

#endif
