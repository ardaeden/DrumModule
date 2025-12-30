#include "i2s.h"

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000UL)

#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define SPI2_BASE (APB1PERIPH_BASE + 0x3800UL)

/* RCC Registers */
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_CR (*(volatile uint32_t *)(RCC_BASE + 0x00))

/* GPIOB Registers */
#define GPIOB_MODER (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_AFRH (*(volatile uint32_t *)(GPIOB_BASE + 0x24))
#define GPIOB_OSPEEDR (*(volatile uint32_t *)(GPIOB_BASE + 0x08))

/* SPI2/I2S2 Registers */
#define SPI2_I2SCFGR (*(volatile uint32_t *)(SPI2_BASE + 0x1C))
#define SPI2_I2SPR (*(volatile uint32_t *)(SPI2_BASE + 0x20))
#define SPI2_CR2 (*(volatile uint32_t *)(SPI2_BASE + 0x04))

/* RCC Control Register Bits */
#define RCC_CR_PLLI2SON (1 << 26)
#define RCC_CR_PLLI2SRDY (1 << 27)

/**
 * @brief Initialize I2S2 for PCM5102A audio DAC
 * @details Configures I2S2 in master transmit mode, 16-bit, ~48kHz
 *          3-wire mode (no MCLK)
 * @return 0 on success, 1 if PLLI2S not ready
 */
int I2S_Init(void) {
  /* Enable peripheral clocks */
  RCC_AHB1ENR |= (1 << 1);  /* GPIOB */
  RCC_APB1ENR |= (1 << 14); /* SPI2 */

  /* Verify PLLI2S is ready (configured in SystemClock_Config) */
  if (!(RCC_CR & RCC_CR_PLLI2SRDY)) {
    return 1;
  }

  /* Configure GPIO pins for I2S2
   * PB10: I2S2_CK  (Bit Clock)
   * PB12: I2S2_WS  (Word Select / LRCK)
   * PB15: I2S2_SD  (Serial Data)
   */

  /* Set PB10, PB12, PB15 to Alternate Function mode */
  GPIOB_MODER &= ~((3UL << 20) | (3UL << 24) | (3UL << 30));
  GPIOB_MODER |= ((2UL << 20) | (2UL << 24) | (2UL << 30));

  /* Set AF5 (I2S2) for PB10, PB12, PB15 */
  GPIOB_AFRH &= ~((0xFUL << 8) | (0xFUL << 16) | (0xFUL << 28));
  GPIOB_AFRH |= ((5UL << 8) | (5UL << 16) | (5UL << 28));

  /* Set very high speed for signal integrity */
  GPIOB_OSPEEDR |= ((3UL << 20) | (3UL << 24) | (3UL << 30));

  /* Configure I2S2
   * Mode: I2S (not SPI)
   * Configuration: Master Transmit
   * Standard: Philips
   * Data length: 16-bit
   */
  SPI2_I2SCFGR = 0;
  SPI2_I2SCFGR |= (1 << 11); /* I2SMOD: I2S mode */
  SPI2_I2SCFGR |= (2 << 8);  /* I2SCFG: Master transmit */

  /* Configure I2S clock prescaler
   * I2SCLK = 64MHz (from PLLI2S)
   * Target sample rate: ~48kHz
   * I2SDIV = 21 achieves approximately 48kHz
   */
  uint16_t i2sdiv = 21;
  uint16_t odd = 0;
  uint16_t mckoe = 0; /* No master clock output (3-wire mode) */

  SPI2_I2SPR = (mckoe << 9) | (odd << 8) | i2sdiv;

  /* Enable DMA request for transmit */
  SPI2_CR2 |= (1 << 1);

  return 0;
}

/**
 * @brief Start I2S transmission
 */
void I2S_Start(void) { SPI2_I2SCFGR |= (1 << 10); /* I2SE: Enable I2S */ }
