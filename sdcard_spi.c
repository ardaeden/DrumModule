#include "sdcard_spi.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define SPI3_BASE (APB1PERIPH_BASE + 0x3C00UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))

/* GPIOB Registers */
#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPER (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPEEDR (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPDR (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_ODR (*(volatile uint32_t *)(GPIOB_BASE + 0x14))
#define GPIOB_BSRR (*(volatile uint32_t *)(GPIOB_BASE + 0x18))
#define GPIOB_AFRL (*(volatile uint32_t *)(GPIOB_BASE + 0x20))

/* SPI3 Registers */
#define SPI3_CR1 (*(volatile uint32_t *)(SPI3_BASE + 0x00))
#define SPI3_SR (*(volatile uint32_t *)(SPI3_BASE + 0x08))
#define SPI3_DR (*(volatile uint32_t *)(SPI3_BASE + 0x0C))

/* SPI Control Register Bits */
#define SPI_CR1_MSTR (1 << 2)
#define SPI_CR1_SPE (1 << 6)
#define SPI_CR1_SSM (1 << 9)
#define SPI_CR1_SSI (1 << 8)
#define SPI_CR1_BR_DIV256 (7 << 3) /* ~187.5kHz at 48MHz APB1 */
#define SPI_CR1_BR_DIV16 (3 << 3)  /* ~3MHz at 48MHz APB1 */

/* SPI Status Register Bits */
#define SPI_SR_TXE (1 << 1)
#define SPI_SR_RXNE (1 << 0)
#define SPI_SR_BSY (1 << 7)

/**
 * @brief Initialize SPI3 for SD card communication
 * @note JTAG is automatically disabled when PB3/PB4 are configured as AF6
 */
void SDCARD_SPI_Init(void) {
  /* Enable peripheral clocks */
  RCC_AHB1ENR |= (1 << 1);  /* GPIOB */
  RCC_APB1ENR |= (1 << 15); /* SPI3 */

  /* Configure PB0 as output (CS) */
  GPIOB_MODER &= ~(3UL << (0 * 2));
  GPIOB_MODER |= (1UL << (0 * 2));   /* Output mode */
  GPIOB_OTYPER &= ~(1 << 0);         /* Push-pull */
  GPIOB_OSPEEDR |= (3UL << (0 * 2)); /* High speed */
  GPIOB_ODR |= (1 << 0);             /* CS high (deselected) */

  /* Configure PB3 (SCK), PB4 (MISO), PB5 (MOSI) as Alternate Function */
  GPIOB_MODER &= ~((3UL << (3 * 2)) | (3UL << (4 * 2)) | (3UL << (5 * 2)));
  GPIOB_MODER |= ((2UL << (3 * 2)) | (2UL << (4 * 2)) | (2UL << (5 * 2)));

  /* Set high speed for SPI pins */
  GPIOB_OSPEEDR |= (3UL << (3 * 2)) | (3UL << (4 * 2)) | (3UL << (5 * 2));

  /* Set pull-up for MISO (PB4) */
  GPIOB_PUPDR &= ~(3UL << (4 * 2));
  GPIOB_PUPDR |= (1UL << (4 * 2));

  /* Set AF6 (SPI3) for PB3, PB4, PB5 */
  GPIOB_AFRL &= ~((0xFUL << (3 * 4)) | (0xFUL << (4 * 4)) | (0xFUL << (5 * 4)));
  GPIOB_AFRL |= ((6UL << (3 * 4)) | (6UL << (4 * 4)) | (6UL << (5 * 4)));

  /* Configure SPI3: Master, Software NSS, Slow speed for init */
  SPI3_CR1 = 0;
  SPI3_CR1 |= SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_DIV256;

  /* Enable SPI3 */
  SPI3_CR1 |= SPI_CR1_SPE;
}

void SDCARD_SPI_CS_Low(void) {
  GPIOB_BSRR = (1 << (0 + 16)); /* Reset bit 0 (CS low) */
}

void SDCARD_SPI_CS_High(void) {
  GPIOB_BSRR = (1 << 0); /* Set bit 0 (CS high) */
}

uint8_t SDCARD_SPI_TransmitReceive(uint8_t data) {
  /* Wait for transmit buffer empty */
  while (!(SPI3_SR & SPI_SR_TXE))
    ;

  /* Send data */
  SPI3_DR = data;

  /* Wait for receive buffer not empty */
  while (!(SPI3_SR & SPI_SR_RXNE))
    ;

  /* Return received data */
  return (uint8_t)SPI3_DR;
}

void SDCARD_SPI_SetSlowSpeed(void) {
  /* Disable SPI */
  SPI3_CR1 &= ~SPI_CR1_SPE;

  /* Set baud rate to /256 (~187.5kHz) */
  SPI3_CR1 &= ~(7 << 3);
  SPI3_CR1 |= SPI_CR1_BR_DIV256;

  /* Enable SPI */
  SPI3_CR1 |= SPI_CR1_SPE;
}

void SDCARD_SPI_SetFastSpeed(void) {
  /* Disable SPI */
  SPI3_CR1 &= ~SPI_CR1_SPE;

  /* Set baud rate to /16 (~3MHz) */
  SPI3_CR1 &= ~(7 << 3);
  SPI3_CR1 |= SPI_CR1_BR_DIV16;

  /* Enable SPI */
  SPI3_CR1 |= SPI_CR1_SPE;
}
