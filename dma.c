#include "dma.h"
#include "audio_synth.h"

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define DMA1_BASE (AHB1PERIPH_BASE + 0x6000UL)
#define SPI2_BASE (APB1PERIPH_BASE + 0x3800UL)

#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* DMA1 Stream 4 Registers */
#define DMA1_S4CR (*(volatile uint32_t *)(DMA1_BASE + 0x70))
#define DMA1_S4NDTR (*(volatile uint32_t *)(DMA1_BASE + 0x74))
#define DMA1_S4PAR (*(volatile uint32_t *)(DMA1_BASE + 0x78))
#define DMA1_S4M0AR (*(volatile uint32_t *)(DMA1_BASE + 0x7C))

/* DMA Interrupt Registers */
#define DMA1_LISR (*(volatile uint32_t *)(DMA1_BASE + 0x00))
#define DMA1_HISR (*(volatile uint32_t *)(DMA1_BASE + 0x04))
#define DMA1_LIFCR (*(volatile uint32_t *)(DMA1_BASE + 0x08))
#define DMA1_HIFCR (*(volatile uint32_t *)(DMA1_BASE + 0x0C))

#define SPI2_DR (*(volatile uint32_t *)(SPI2_BASE + 0x0C))

/* NVIC */
#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100)

void DMA_Init_I2S(int16_t *buffer, uint32_t len) {
  /* Enable DMA1 clock */
  RCC_AHB1ENR |= (1 << 21);

  /* Disable stream */
  DMA1_S4CR &= ~(1 << 0);
  while (DMA1_S4CR & (1 << 0))
    ;

  /* Clear IRQ flags */
  DMA1_HIFCR = (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 0);

  /* Configure DMA1 Stream 4 */
  uint32_t cr = 0;
  cr |= (0 << 25); /* Channel 0 */
  cr |= (3 << 16); /* Priority: Very High */
  cr |= (1 << 13); /* MSIZE: 16-bit */
  cr |= (1 << 11); /* PSIZE: 16-bit */
  cr |= (1 << 10); /* MINC: Memory increment */
  cr |= (1 << 8);  /* CIRC: Circular mode */
  cr |= (1 << 6);  /* DIR: Memory to Peripheral */
  cr |= (1 << 4);  /* TCIE: Transfer Complete Interrupt Enable */
  cr |= (1 << 3);  /* HTIE: Half Transfer Interrupt Enable */

  DMA1_S4CR = cr;

  DMA1_S4NDTR = len; /* Length is in 16-bit units */
  DMA1_S4PAR = (uint32_t)(uintptr_t)&SPI2_DR;
  DMA1_S4M0AR = (uint32_t)(uintptr_t)buffer;

  /* Enable IRQ in NVIC (DMA1_Stream4 is IRQ 15) */
  NVIC_ISER0 |= (1 << 15);

  /* Enable stream */
  DMA1_S4CR |= (1 << 0);
}

/**
 * @brief DMA1 Stream 4 Interrupt Handler
 * @note Called at Half-Transfer (fills first half) and Transfer-Complete (fills
 * second half)
 */
void DMA1_Stream4_IRQHandler(void) {
  /* Check TCIF4 (Bit 5 in HISR) */
  if (DMA1_HISR & (1 << 5)) {
    DMA1_HIFCR = (1 << 5); /* Clear TC flag */

    /* Transfer Complete: Fill Second Half */
    AudioSynth_FillBuffer(&audio_buffer[AUDIO_BUFFER_SIZE / 2],
                          AUDIO_BUFFER_SIZE / 2);
  }

  /* Check HTIF4 (Bit 4 in HISR) */
  if (DMA1_HISR & (1 << 4)) {
    DMA1_HIFCR = (1 << 4); /* Clear HT flag */

    /* Half Transfer: Fill First Half */
    AudioSynth_FillBuffer(&audio_buffer[0], AUDIO_BUFFER_SIZE / 2);
  }
}
