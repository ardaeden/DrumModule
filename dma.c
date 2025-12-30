#include "dma.h"

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define DMA1_BASE (AHB1PERIPH_BASE + 0x6000UL)
#define SPI2_BASE (APB1PERIPH_BASE + 0x3800UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* DMA1 Stream 4 Registers */
#define DMA1_S4CR (*(volatile uint32_t *)(DMA1_BASE + 0x70)) /* Configuration  \
                                                              */
#define DMA1_S4NDTR                                                            \
  (*(volatile uint32_t *)(DMA1_BASE + 0x74)) /* Number of data */
#define DMA1_S4PAR                                                             \
  (*(volatile uint32_t *)(DMA1_BASE + 0x78)) /* Peripheral address */
#define DMA1_S4M0AR                                                            \
  (*(volatile uint32_t *)(DMA1_BASE + 0x7C)) /* Memory 0 address */
#define DMA1_HIFCR                                                             \
  (*(volatile uint32_t *)(DMA1_BASE + 0x0C)) /* High interrupt flag clear */

/* SPI2 Data Register */
#define SPI2_DR (*(volatile uint32_t *)(SPI2_BASE + 0x0C))

/**
 * @brief Initialize DMA1 Stream 4 for I2S2 transmission
 * @param buffer Pointer to audio sample buffer (16-bit stereo)
 * @param len Number of 16-bit samples to transfer
 * @details Configures DMA in circular mode for continuous audio playback
 */
void DMA_Init_I2S(int16_t *buffer, uint32_t len) {
  /* Enable DMA1 clock */
  RCC_AHB1ENR |= (1 << 21);

  /* Disable stream to allow configuration */
  DMA1_S4CR &= ~(1 << 0);
  while (DMA1_S4CR & (1 << 0))
    ; /* Wait for disable */

  /* Clear all interrupt flags for Stream 4 */
  DMA1_HIFCR = (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 0);

  /* Configure DMA1 Stream 4
   * Channel 0: SPI2_TX
   * Priority: Very High
   * Memory size: 16-bit (halfword)
   * Peripheral size: 16-bit (halfword)
   * Memory increment: Enabled
   * Peripheral increment: Disabled
   * Circular mode: Enabled (continuous playback)
   * Direction: Memory to Peripheral
   */
  uint32_t cr = 0;
  cr |= (0 << 25); /* Channel 0 */
  cr |= (3 << 16); /* Priority: Very High */
  cr |= (1 << 13); /* MSIZE: 16-bit */
  cr |= (1 << 11); /* PSIZE: 16-bit */
  cr |= (1 << 10); /* MINC: Memory increment */
  cr |= (1 << 8);  /* CIRC: Circular mode */
  cr |= (1 << 6);  /* DIR: Memory to Peripheral */

  DMA1_S4CR = cr;

  /* Set transfer parameters */
  DMA1_S4NDTR = len;                          /* Number of data items */
  DMA1_S4PAR = (uint32_t)(uintptr_t)&SPI2_DR; /* Peripheral address */
  DMA1_S4M0AR = (uint32_t)(uintptr_t)buffer;  /* Memory address */

  /* Enable stream */
  DMA1_S4CR |= (1 << 0);
}
