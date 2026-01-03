#include "encoder.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define SYSCFG_BASE (APB2PERIPH_BASE + 0x3800UL)
#define EXTI_BASE (APB2PERIPH_BASE + 0x3C00UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x44))

/* GPIOB Registers */
#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPDR (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_IDR (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

/* SYSCFG Registers */
#define SYSCFG_EXTICR2 (*(volatile uint32_t *)(SYSCFG_BASE + 0x0C))
#define SYSCFG_EXTICR3 (*(volatile uint32_t *)(SYSCFG_BASE + 0x10))

/* EXTI Registers */
#define EXTI_IMR (*(volatile uint32_t *)(EXTI_BASE + 0x00))
#define EXTI_RTSR (*(volatile uint32_t *)(EXTI_BASE + 0x08))
#define EXTI_FTSR (*(volatile uint32_t *)(EXTI_BASE + 0x0C))
#define EXTI_PR (*(volatile uint32_t *)(EXTI_BASE + 0x14))

/* NVIC Registers */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100)

/* Encoder pins */
#define ENC_A_PIN 6  /* PB6 */
#define ENC_B_PIN 7  /* PB7 */
#define ENC_SW_PIN 8 /* PB8 */

/* Encoder state */
static volatile int32_t encoder_value = 0;
static volatile int32_t encoder_min = -1000000;
static volatile int32_t encoder_max = 1000000;
static volatile uint8_t last_a = 0;
static volatile uint8_t button_clicked = 0;
static volatile uint32_t last_button_time = 0;
static volatile int32_t increment_step = 1; /* 1 or 10 */
static volatile uint8_t last_button_state =
    1; /* Active low, so 1 = not pressed */

/* Debounce time in milliseconds */
#define DEBOUNCE_MS 50

/**
 * @brief Simple delay for debouncing
 */
static void delay_us(uint32_t us) {
  volatile uint32_t count = us * 24; /* Approximate for 96MHz */
  while (count--)
    ;
}

void Encoder_Init(void) {
  /* Enable clocks */
  RCC_AHB1ENR |= (1 << 1);  /* GPIOB */
  RCC_APB2ENR |= (1 << 14); /* SYSCFG */

  /* Configure PB6, PB7, PB8 as inputs with pull-ups */
  GPIOB_MODER &= ~((3UL << (ENC_A_PIN * 2)) | (3UL << (ENC_B_PIN * 2)) |
                   (3UL << (ENC_SW_PIN * 2)));

  /* Enable pull-ups for Encoder A, B and Button */
  GPIOB_PUPDR &= ~((3UL << (ENC_A_PIN * 2)) | (3UL << (ENC_B_PIN * 2)) |
                   (3UL << (ENC_SW_PIN * 2)));
  GPIOB_PUPDR |= ((1UL << (ENC_A_PIN * 2)) | (1UL << (ENC_B_PIN * 2)) |
                  (1UL << (ENC_SW_PIN * 2)));

  /* Note: Interrupt usage is managed by buttons.c, but GPIO config is here */
  uint8_t a = (GPIOB_IDR >> ENC_A_PIN) & 1;
  last_a = a;
}

int32_t Encoder_GetValue(void) { return encoder_value; }

void Encoder_SetValue(int32_t value) { encoder_value = value; }

void Encoder_SetLimits(int32_t min, int32_t max) {
  encoder_min = min;
  encoder_max = max;
}

int32_t Encoder_GetIncrementStep(void) { return increment_step; }

void Encoder_ToggleIncrement(void) {
  if (increment_step == 1) {
    increment_step = 10;
  } else {
    increment_step = 1;
  }
}

void Encoder_ResetIncrement(void) { increment_step = 1; }

/**
 * @brief Handle rotation logic (called from EXTI ISR in buttons.c)
 */
void Encoder_HandleRotation(void) {
  /* Reduced delay for debouncing to improve responsiveness/stability */
  delay_us(10);

  /* Read current state */
  uint8_t a = (GPIOB_IDR >> ENC_A_PIN) & 1;
  uint8_t b = (GPIOB_IDR >> ENC_B_PIN) & 1;

  /* Detect on both rising and falling edges of A */
  if (a != last_a) {
    if (a == 1) {
      /* Rising edge on A */
      if (b == 1) {
        encoder_value += increment_step; /* CW */
      } else {
        encoder_value -= increment_step; /* CCW */
      }
    } else {
      /* Falling edge on A */
      if (b == 0) {
        encoder_value += increment_step; /* CW */
      } else {
        encoder_value -= increment_step; /* CCW */
      }
    }

    /* Apply limits */
    if (encoder_value < encoder_min)
      encoder_value = encoder_min;
    if (encoder_value > encoder_max)
      encoder_value = encoder_max;

    last_a = a;
  }
}
