#include "spi.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000UL)
#define SPI1_BASE (APB2PERIPH_BASE + 0x3000UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x44))

/* GPIOA Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_OSPEEDR (*(volatile uint32_t *)(GPIOA_BASE + 0x08))

/* SPI1 Registers */
#define SPI1_CR1 (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI1_SR (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_DR (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

/* SPI Control Register Bits */
#define SPI_CR1_MSTR (1 << 2)    /* Master mode */
#define SPI_CR1_SPE (1 << 6)     /* SPI enable */
#define SPI_CR1_SSM (1 << 9)     /* Software slave management */
#define SPI_CR1_SSI (1 << 8)     /* Internal slave select */
#define SPI_CR1_BR_DIV2 (0 << 3) /* Baud rate: fPCLK/2 */

/* SPI Status Register Bits */
#define SPI_SR_TXE (1 << 1) /* Transmit buffer empty */
#define SPI_SR_BSY (1 << 7) /* Busy flag */

/**
 * @brief Initialize SPI1 for ST7789 display
 * @details Configures PA5 (SCK) and PA7 (MOSI) as AF5
 *          SPI speed: 48MHz (APB2/2)
 */
void SPI_Init(void) {
  /* Enable peripheral clocks */
  RCC_AHB1ENR |= (1 << 0);  /* GPIOA */
  RCC_APB2ENR |= (1 << 12); /* SPI1 */

  /* Configure PA5 and PA7 as Alternate Function */
  GPIOA_MODER &= ~((3UL << (5 * 2)) | (3UL << (7 * 2)));
  GPIOA_MODER |= ((2UL << (5 * 2)) | (2UL << (7 * 2)));

  /* Set high speed for SPI pins */
  GPIOA_OSPEEDR |= (3UL << (5 * 2)) | (3UL << (7 * 2));

  /* Set AF5 (SPI1) for PA5 and PA7 */
  GPIOA_AFRL &= ~((0xFUL << (5 * 4)) | (0xFUL << (7 * 4)));
  GPIOA_AFRL |= ((5UL << (5 * 4)) | (5UL << (7 * 4)));

  /* Configure SPI1: Master, Software NSS, 48MHz */
  SPI1_CR1 = 0;
  SPI1_CR1 |= SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_DIV2;

  /* Enable SPI1 */
  SPI1_CR1 |= SPI_CR1_SPE;
}

/**
 * @brief Transmit one byte via SPI1 (Safe version)
 * @details Waits for both TXE and BSY to ensure clean line state
 * @param data Byte to transmit
 */
void SPI_Transmit(uint8_t data) {
  /* Wait for transmit buffer empty */
  while (!(SPI1_SR & SPI_SR_TXE))
    ;

  /* Send data */
  SPI1_DR = data;

  /* Wait for transmission complete */
  while (SPI1_SR & SPI_SR_BSY)
    ;
}

/**
 * @brief Write one byte to SPI1 as fast as possible
 * @details Only waits for TXE (Transpose Empty). Used for bulk transfers.
 * @param data Byte to transmit
 */
void SPI_WriteData8(uint8_t data) {
  /* Wait for transmit buffer empty */
  while (!(SPI1_SR & SPI_SR_TXE))
    ;

  /* Send data */
  SPI1_DR = data;
}

/**
 * @brief Write one 16-bit word to SPI1 as fast as possible
 * @details Only waits for TXE (Transpose Empty). Used for bulk transfers.
 * @param data 16-bit word to transmit
 */
void SPI_WriteData16(uint16_t data) {
  /* Wait for transmit buffer empty */
  while (!(SPI1_SR & SPI_SR_TXE))
    ;

  /* Send data */
  SPI1_DR = data;
}

/**
 * @brief Configure SPI1 for 16-bit data frame format
 */
void SPI_SetDataSize16(void) {
  SPI1_CR1 &= ~SPI_CR1_SPE; /* Disable SPI */
  SPI1_CR1 |= (1 << 11);    /* DFF: 16-bit data frame format */
  SPI1_CR1 |= SPI_CR1_SPE;  /* Enable SPI */
}

/**
 * @brief Configure SPI1 for 8-bit data frame format
 */
void SPI_SetDataSize8(void) {
  SPI1_CR1 &= ~SPI_CR1_SPE; /* Disable SPI */
  SPI1_CR1 &= ~(1 << 11);   /* DFF: 8-bit data frame format */
  SPI1_CR1 |= SPI_CR1_SPE;  /* Enable SPI */
}

/**
 * @brief Wait until SPI1 is no longer busy
 * @details Must be called after bulk transfers before toggling CS/DC pins
 */
void SPI_WaitBusy(void) {
  while (SPI1_SR & SPI_SR_BSY)
    ;
}
