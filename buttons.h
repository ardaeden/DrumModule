#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/* Button IDs */
typedef enum {
  BUTTON_START = 0,
  BUTTON_ENCODER = 1,
  BUTTON_EDIT = 2,
  BUTTON_PATTERN = 3
} ButtonID;

/**
 * @brief Button event callback type
 * @param button_id ID of the button (BUTTON_START, BUTTON_ENCODER, BUTTON_EDIT,
 * or BUTTON_PATTERN)
 * @param pressed 1 if button pressed, 0 if released
 */
typedef void (*ButtonCallback)(ButtonID button_id, uint8_t pressed);

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
