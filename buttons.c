#include "buttons.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000UL)
#define SYSCFG_BASE (APB2PERIPH_BASE + 0x3800UL)
#define EXTI_BASE (APB2PERIPH_BASE + 0x3C00UL)
#define TIM5_BASE (APB1PERIPH_BASE + 0x0C00UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x44))

/* GPIOA Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_IDR (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

/* SYSCFG Registers */
#define SYSCFG_EXTICR1 (*(volatile uint32_t *)(SYSCFG_BASE + 0x08))

/* EXTI Registers */
#define EXTI_IMR (*(volatile uint32_t *)(EXTI_BASE + 0x00))
#define EXTI_RTSR (*(volatile uint32_t *)(EXTI_BASE + 0x08))
#define EXTI_FTSR (*(volatile uint32_t *)(EXTI_BASE + 0x0C))
#define EXTI_PR (*(volatile uint32_t *)(EXTI_BASE + 0x14))

/* TIM5 Registers */
#define TIM5_CR1 (*(volatile uint32_t *)(TIM5_BASE + 0x00))
#define TIM5_DIER (*(volatile uint32_t *)(TIM5_BASE + 0x0C))
#define TIM5_SR (*(volatile uint32_t *)(TIM5_BASE + 0x10))
#define TIM5_PSC (*(volatile uint32_t *)(TIM5_BASE + 0x28))
#define TIM5_ARR (*(volatile uint32_t *)(TIM5_BASE + 0x2C))

/* NVIC Registers */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100)
#define NVIC_ISER1 (*(volatile uint32_t *)0xE000E104)

/* Callback */
static ButtonCallback button_callback = 0;

void Button_Init(void) {
  /* Enable clocks */
  RCC_AHB1ENR |= (1 << 0);  /* GPIOA */
  RCC_APB2ENR |= (1 << 14); /* SYSCFG */
  RCC_APB1ENR |= (1 << 3);  /* TIM5 */

  /* Configure PA0 as input with pull-up */
  GPIOA_MODER &= ~(3UL << (0 * 2)); /* Input mode */
  GPIOA_PUPDR &= ~(3UL << (0 * 2));
  GPIOA_PUPDR |= (1UL << (0 * 2)); /* Pull-up */

  /* Connect PA0 to EXTI0 */
  SYSCFG_EXTICR1 &= ~(0xF << 0); /* Clear EXTI0 bits */
  SYSCFG_EXTICR1 |= (0 << 0);    /* PA0 -> EXTI0 */

  /* Configure EXTI0 for falling edge (button press) */
  EXTI_FTSR |= (1 << 0);  /* Falling edge trigger */
  EXTI_RTSR &= ~(1 << 0); /* No rising edge */
  EXTI_IMR |= (1 << 0);   /* Unmask EXTI0 */

  /* Configure TIM5 for 20ms debounce */
  /* APB1 = 48MHz, PSC = 47999 -> 1kHz, ARR = 20 -> 20ms */
  TIM5_PSC = 47999;
  TIM5_ARR = 20;
  TIM5_DIER |= (1 << 0); /* Enable update interrupt */

  /* Enable EXTI0 interrupt (IRQ 6) */
  NVIC_ISER0 |= (1 << 6);

  /* Enable TIM5 interrupt (IRQ 50) */
  NVIC_ISER1 |= (1 << (50 - 32));
}

void Button_SetCallback(ButtonCallback callback) { button_callback = callback; }

/**
 * @brief EXTI0 Interrupt Handler
 */
void EXTI0_IRQHandler(void) {
  if (EXTI_PR & (1 << 0)) {
    /* Clear pending bit */
    EXTI_PR = (1 << 0);

    /* Disable EXTI0 to prevent bouncing */
    EXTI_IMR &= ~(1 << 0);

    /* Start debounce timer */
    TIM5_CR1 |= (1 << 0); /* Enable timer */
  }
}

/**
 * @brief TIM5 Interrupt Handler (Debounce timer)
 */
void TIM5_IRQHandler(void) {
  if (TIM5_SR & (1 << 0)) {
    /* Clear update flag */
    TIM5_SR &= ~(1 << 0);

    /* Stop timer */
    TIM5_CR1 &= ~(1 << 0);

    /* Read stable button state */
    uint8_t btn_state = (GPIOA_IDR & (1 << 0)) ? 1 : 0;

    /* Call callback if registered */
    if (button_callback) {
      /* Active low: 0 = pressed, 1 = released */
      button_callback(btn_state == 0 ? 1 : 0);
    }

    /* Re-enable EXTI0 */
    EXTI_IMR |= (1 << 0);
  }
}
