#include "dma_spi.h"
#include <stddef.h>
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define DMA2_BASE (AHB1PERIPH_BASE + 0x6400UL)
#define SPI1_BASE (APB2PERIPH_BASE + 0x3000UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))

/* DMA2 Stream 3 Registers (SPI1_TX) */
#define DMA2_S3CR (*(volatile uint32_t *)(DMA2_BASE + 0x58))
#define DMA2_S3NDTR (*(volatile uint32_t *)(DMA2_BASE + 0x5C))
#define DMA2_S3PAR (*(volatile uint32_t *)(DMA2_BASE + 0x60))
#define DMA2_S3M0AR (*(volatile uint32_t *)(DMA2_BASE + 0x64))
#define DMA2_LISR (*(volatile uint32_t *)(DMA2_BASE + 0x00))
#define DMA2_LIFCR (*(volatile uint32_t *)(DMA2_BASE + 0x08))

/* SPI1 Registers */
#define SPI1_CR1 (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI1_CR2 (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_SR (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

/* DMA Register Bits */
#define DMA_SxCR_EN (1 << 0)
#define DMA_SxCR_DIR_M2P (1 << 6)
#define DMA_SxCR_MINC (1 << 10)
#define DMA_SxCR_PSIZE_16 (1 << 11)
#define DMA_SxCR_MSIZE_16 (1 << 13)
#define DMA_SxCR_PL_HIGH (2 << 16)
#define DMA_SxCR_CHSEL_3 (3 << 25)

/* DMA Status Bits (Stream 3) */
#define DMA_LISR_TCIF3 (1 << 27)

/* SPI Control Register Bits */
#define SPI_CR2_TXDMAEN (1 << 1)

/* Static buffer for color - use 1024 pixels (2KB) for chunked transfers */
#define DMA_BUFFER_SIZE 1024
static uint16_t dma_color_buffer[DMA_BUFFER_SIZE];

/**
 * @brief Initialize DMA2 Stream 3 for SPI1 TX
 */
void SPI_DMA_Init(void) {
  /* Enable DMA2 clock */
  RCC_AHB1ENR |= (1 << 22);
}

/**
 * @brief Fill area with color using DMA (Zero CPU overhead)
 * @param color RGB565 color value
 * @param count Number of pixels to fill
 */
void SPI_DMA_FillColor(uint16_t color, uint32_t count) {
  if (count == 0)
    return;
  dma_color_buffer[0] = color;

  /* Use raw transfer function with MINC=0 */
  SPI_DMA_StartTransfer(&dma_color_buffer[0], count, 0);
}

/**
 * @brief Start a raw DMA transfer (No pin toggling)
 * @param addr Source memory address
 * @param count Number of 16-bit items
 * @param minc 1 for memory increment, 0 for fixed address
 */
void SPI_DMA_StartTransfer(void *addr, uint32_t count, uint8_t minc) {
  if (count == 0 || addr == NULL)
    return;

  /* Disable DMA stream */
  DMA2_S3CR &= ~DMA_SxCR_EN;
  while (DMA2_S3CR & DMA_SxCR_EN)
    ;

  /* Clear ALL transfer flags for Stream 3 to ensure clean state */
  DMA2_LIFCR = (0x3D << 22); /* Clear FEIF3, DMEIF3, TEIF3, HTIF3, TCIF3 */

  /* Configure Stream 3 */
  uint32_t cr = DMA_SxCR_CHSEL_3 | DMA_SxCR_DIR_M2P | DMA_SxCR_PSIZE_16 |
                DMA_SxCR_MSIZE_16 | DMA_SxCR_PL_HIGH;
  if (minc)
    cr |= DMA_SxCR_MINC;

  DMA2_S3CR = cr;

  /* Set addresses and count */
  DMA2_S3PAR = (uint32_t)(uintptr_t)&SPI1_DR;
  DMA2_S3M0AR = (uint32_t)(uintptr_t)addr;
  DMA2_S3NDTR = count;

  /* Ensure SPI TX DMA is enabled */
  SPI1_CR2 |= SPI_CR2_TXDMAEN;

  /* Start! */
  DMA2_S3CR |= DMA_SxCR_EN;

  /* Wait for completion */
  while (!(DMA2_LISR & DMA_LISR_TCIF3))
    ;

  /* Wait for SPI to finish last bits */
  while (SPI1_SR & (1 << 7))
    ;

  /* Disable DMA stream to ensure clean state */
  DMA2_S3CR &= ~DMA_SxCR_EN;

  /* Disable SPI TX DMA request to prevent state leakage */
  SPI1_CR2 &= ~SPI_CR2_TXDMAEN;
}
