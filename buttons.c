#include "buttons.h"
#include "encoder.h" /* For Encoder_HandleRotation */
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define SYSCFG_BASE (APB2PERIPH_BASE + 0x3800UL)
#define EXTI_BASE (APB2PERIPH_BASE + 0x3C00UL)
#define TIM5_BASE (APB1PERIPH_BASE + 0x0C00UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x44))

/* GPIO Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_IDR (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

#define GPIOB_IDR (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

/* SYSCFG Registers */
#define SYSCFG_EXTICR1 (*(volatile uint32_t *)(SYSCFG_BASE + 0x08))
#define SYSCFG_EXTICR2 (*(volatile uint32_t *)(SYSCFG_BASE + 0x0C))
#define SYSCFG_EXTICR3 (*(volatile uint32_t *)(SYSCFG_BASE + 0x10))

/* EXTI Registers */
#define EXTI_IMR (*(volatile uint32_t *)(EXTI_BASE + 0x00))
#define EXTI_RTSR (*(volatile uint32_t *)(EXTI_BASE + 0x08))
#define EXTI_FTSR (*(volatile uint32_t *)(EXTI_BASE + 0x0C))
#define EXTI_PR (*(volatile uint32_t *)(EXTI_BASE + 0x14))

/* TIM5 Registers */
#define TIM5_CR1 (*(volatile uint32_t *)(TIM5_BASE + 0x00))
#define TIM5_DIER (*(volatile uint32_t *)(TIM5_BASE + 0x0C))
#define TIM5_SR (*(volatile uint32_t *)(TIM5_BASE + 0x10))
#define TIM5_CNT (*(volatile uint32_t *)(TIM5_BASE + 0x24))
#define TIM5_PSC (*(volatile uint32_t *)(TIM5_BASE + 0x28))
#define TIM5_ARR (*(volatile uint32_t *)(TIM5_BASE + 0x2C))

/* NVIC Registers */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100)
#define NVIC_ISER1 (*(volatile uint32_t *)0xE000E104)

/* State */
static ButtonCallback button_callback = 0;
static volatile uint8_t buttons_active_mask =
    0; /* Bit 0: Start, Bit 1: Encoder */

void Button_Init(void) {
  /* Enable clocks */
  RCC_AHB1ENR |= (1 << 0);  /* GPIOA */
  RCC_AHB1ENR |= (1 << 1);  /* GPIOB */
  RCC_APB2ENR |= (1 << 14); /* SYSCFG */
  RCC_APB1ENR |= (1 << 3);  /* TIM5 */

  /* --- Configure PA0 (Start/Stop) --- */
  /* Input, Pull-Up */
  GPIOA_MODER &= ~(3UL << (0 * 2));
  GPIOA_PUPDR &= ~(3UL << (0 * 2));
  GPIOA_PUPDR |= (1UL << (0 * 2));

  /* PA0 -> EXTI0 */
  SYSCFG_EXTICR1 &= ~(0xF << 0);
  SYSCFG_EXTICR1 |= (0 << 0);

  /* EXTI0 Falling edge */
  EXTI_FTSR |= (1 << 0);
  EXTI_RTSR &= ~(1 << 0);
  EXTI_IMR |= (1 << 0);
  NVIC_ISER0 |= (1 << 6); /* IRQ 6 */

  /* --- Configure PB8 (Encoder Button), PB6, PB7 --- */
  /* Note: Encoder_Init already sets pull-ups for B6-B8 */

  /* PB6, PB7 -> EXTI6, EXTI7 (Port B = 0001) */
  SYSCFG_EXTICR2 &= ~((0xF << 8) | (0xF << 12));
  SYSCFG_EXTICR2 |= ((1 << 8) | (1 << 12));

  /* PB8 -> EXTI8 (Port B = 0001) */
  SYSCFG_EXTICR3 &= ~(0xF << 0);
  SYSCFG_EXTICR3 |= (1 << 0);

  /* EXTI6, EXTI7: Both edges (Encoder Rotation) */
  EXTI_RTSR |= (1 << 6) | (1 << 7);
  EXTI_FTSR |= (1 << 6) | (1 << 7);

  /* EXTI8: Falling edge (Button Press, Active Low) */
  EXTI_FTSR |= (1 << 8);
  EXTI_RTSR &= ~(1 << 8);

  /* Unmask EXTI6, EXTI7, EXTI8 */
  EXTI_IMR |= (1 << 6) | (1 << 7) | (1 << 8);

  /* Enable EXTI9_5 interrupt in NVIC (IRQ 23) */
  NVIC_ISER0 |= (1 << 23);

  /* --- Configure TIM5 for debounce --- */
  TIM5_PSC = 47999;      /* 1ms tick */
  TIM5_ARR = 20;         /* 20ms */
  TIM5_DIER |= (1 << 0); /* UIE */
  NVIC_ISER1 |= (1 << (50 - 32));
}

void Button_SetCallback(ButtonCallback callback) { button_callback = callback; }

void EXTI0_IRQHandler(void) {
  if (EXTI_PR & (1 << 0)) {
    EXTI_PR = (1 << 0);
    EXTI_IMR &= ~(1 << 0); /* Mask EXTI0 */
    buttons_active_mask |= (1 << BUTTON_START);
    TIM5_CNT = 0;
    TIM5_CR1 |= (1 << 0); /* Start timer */
  }
}

void EXTI9_5_IRQHandler(void) {
  uint32_t pr = EXTI_PR;

  /* Check PB6 (Enc A) / PB7 (Enc B) */
  if (pr & ((1 << 6) | (1 << 7))) {
    EXTI_PR = ((1 << 6) | (1 << 7));
    Encoder_HandleRotation();
  }

  /* Check PB8 (Enc Switch) */
  if (pr & (1 << 8)) {
    EXTI_PR = (1 << 8);
    EXTI_IMR &= ~(1 << 8); /* Mask EXTI8 */
    buttons_active_mask |= (1 << BUTTON_ENCODER);
    TIM5_CNT = 0;
    TIM5_CR1 |= (1 << 0); /* Start timer */
  }
}

void TIM5_IRQHandler(void) {
  if (TIM5_SR & (1 << 0)) {
    TIM5_SR &= ~(1 << 0);
    TIM5_CR1 &= ~(1 << 0); /* Stop timer */

    /* Check Start Button (PA0) */
    if (buttons_active_mask & (1 << BUTTON_START)) {
      uint8_t val = (GPIOA_IDR & (1 << 0)) ? 1 : 0;
      if (val == 0) {
        if (button_callback)
          button_callback(BUTTON_START, 1);
      }
      EXTI_IMR |= (1 << 0); /* Unmask */
    }

    /* Check Encoder Button (PB8) */
    if (buttons_active_mask & (1 << BUTTON_ENCODER)) {
      uint8_t val = (GPIOB_IDR & (1 << 8)) ? 1 : 0;
      if (val == 0) {
        if (button_callback)
          button_callback(BUTTON_ENCODER, 1);
      }
      EXTI_IMR |= (1 << 8); /* Unmask */
    }

    buttons_active_mask = 0;
  }
}
