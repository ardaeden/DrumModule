#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/* Button IDs */
#define BUTTON_START 0
#define BUTTON_ENCODER 1
#define BUTTON_EDIT 2

/**
 * @brief Button event callback type
 * @param button_id ID of the button (BUTTON_START or BUTTON_ENCODER)
 * @param pressed 1 if button pressed, 0 if released
 */
typedef void (*ButtonCallback)(uint8_t button_id, uint8_t pressed);

/**
 * @brief Initialize button with hardware interrupt
 * @details Configures PA0 with EXTI0 interrupt and TIM5 for debouncing
 */
void Button_Init(void);

/**
 * @brief Set callback for button events
 * @param callback Function to call on button press/release
 */
void Button_SetCallback(ButtonCallback callback);

#endif
