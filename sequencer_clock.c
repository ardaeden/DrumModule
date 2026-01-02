#include "sequencer_clock.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)

#define TIM2_BASE (APB1PERIPH_BASE + 0x0000UL)
#define RCC_BASE (PERIPH_BASE + 0x00023800UL)

/* TIM2 Registers */
#define TIM2_CR1 (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_DIER (*(volatile uint32_t *)(TIM2_BASE + 0x0C))
#define TIM2_SR (*(volatile uint32_t *)(TIM2_BASE + 0x10))
#define TIM2_EGR (*(volatile uint32_t *)(TIM2_BASE + 0x14))
#define TIM2_CCMR1 (*(volatile uint32_t *)(TIM2_BASE + 0x18))
#define TIM2_CCER (*(volatile uint32_t *)(TIM2_BASE + 0x20))
#define TIM2_CNT (*(volatile uint32_t *)(TIM2_BASE + 0x24))
#define TIM2_PSC (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
#define TIM2_CCR1 (*(volatile uint32_t *)(TIM2_BASE + 0x34))

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* GPIOA Registers */
#define GPIOA_BASE (PERIPH_BASE + 0x00020000UL) /* AHB1 Base */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRH (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

/* TIM2 Control Register Bits */
#define TIM_CR1_CEN (1 << 0)  /* Counter enable */
#define TIM_CR1_URS (1 << 2)  /* Update request source */
#define TIM_DIER_UIE (1 << 0) /* Update interrupt enable */
#define TIM_SR_UIF (1 << 0)   /* Update interrupt flag */

/* NVIC Registers */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100)

/* Clock state */
static volatile uint16_t current_bpm = 120;
static volatile uint8_t clock_running = 0;
static volatile uint8_t current_pulse = 0;
static ClockCallback clock_callback = 0;

/* Timer frequency: 96MHz / 96 = 1MHz */
#define TIMER_FREQ 1000000UL

/**
 * @brief Calculate timer period for given BPM
 * @param bpm Beats per minute
 * @return Timer auto-reload value
 */
static uint32_t calculate_period(uint16_t bpm) {
  /* Formula: Period = 60,000,000 / (BPM × 24)
   * At 1MHz timer clock: Period = 60,000 / (BPM × 24) × 1000
   * Simplified: Period = 2,500,000 / BPM
   */
  return (TIMER_FREQ * 60) / (bpm * 24);
}

void Clock_Init(void) {
  /* Enable TIM2 clock */
  RCC_APB1ENR |= (1 << 0);

  /* Enable GPIOA clock for PA15 */
  RCC_AHB1ENR |= (1 << 0);

  /* Configure PA15 as Alternate Function (Mode 10) */
  GPIOA_MODER &= ~(3UL << (15 * 2));
  GPIOA_MODER |= (2UL << (15 * 2));

  /* Set PA15 to AF1 (TIM2_CH1) */
  /* AFR[1] is AFRH. Pin 15 is in AFRH bits 28-31 */
  GPIOA_AFRH &= ~(0xFUL << 28);
  GPIOA_AFRH |= (1UL << 28);

  /* Configure TIM2 */
  TIM2_CR1 = 0;
  TIM2_CR1 |= (1 << 7); /* ARPE: Auto-reload preload enable */
  TIM2_PSC = 95;        /* 96MHz / 96 = 1MHz */
  TIM2_ARR = calculate_period(current_bpm) - 1;
  TIM2_CNT = 0;

  /* Configure TIM2 Channel 1 for PWM Mode 1 (Output) */
  /* CCMR1: OC1M = 110 (PWM Mode 1), OC1PE = 1 (Preload enable) */
  TIM2_CCMR1 &= ~0xFF; /* Clear Channel 1 bits */
  TIM2_CCMR1 |= (6 << 4) | (1 << 3);

  /* Set Duty Cycle to 50% */
  TIM2_CCR1 = (TIM2_ARR + 1) / 2;

  /* Enable Output on Channel 1 */
  /* CCER: CC1E = 1 */
  TIM2_CCER |= (1 << 0);

  /* Enable update interrupt */
  TIM2_DIER |= TIM_DIER_UIE;

  /* Enable TIM2 interrupt in NVIC (IRQ 28) */
  NVIC_ISER0 |= (1 << 28);

  /* Update registers */
  TIM2_CR1 |= TIM_CR1_URS;

  /* Force update generation to load ARR shadow register immediately */
  TIM2_EGR |= (1 << 0);   /* UG */
  TIM2_SR &= ~TIM_SR_UIF; /* Clear update flag just in case */
}

void Clock_SetBPM(uint16_t bpm) {
  /* Clamp BPM to valid range */
  if (bpm < 40)
    bpm = 40;
  if (bpm > 300)
    bpm = 300;

  current_bpm = bpm;

  /* Update timer period */
  uint32_t period = calculate_period(bpm);
  TIM2_ARR = period - 1;

  /* Update Duty Cycle to 50% */
  TIM2_CCR1 = period / 2;
}

uint16_t Clock_GetBPM(void) { return current_bpm; }

void Clock_Start(void) {
  clock_running = 1;
  current_pulse = 0;
  TIM2_CNT = 0;
  TIM2_CR1 |= TIM_CR1_CEN;
}

void Clock_Stop(void) {
  clock_running = 0;
  TIM2_CR1 &= ~TIM_CR1_CEN;
  current_pulse = 0;
}

uint8_t Clock_IsRunning(void) { return clock_running; }

uint8_t Clock_GetPulse(void) { return current_pulse; }

void Clock_SetCallback(ClockCallback callback) { clock_callback = callback; }

/**
 * @brief TIM2 interrupt handler
 */
void TIM2_IRQHandler(void) {
  if (TIM2_SR & TIM_SR_UIF) {
    /* Clear interrupt flag */
    TIM2_SR = ~TIM_SR_UIF;

    if (clock_running) {
      /* Call callback if set */
      if (clock_callback) {
        clock_callback(current_pulse);
      }

      /* Increment pulse counter */
      current_pulse++;
      if (current_pulse >= 24) {
        current_pulse = 0;
      }
    }
  }
}
