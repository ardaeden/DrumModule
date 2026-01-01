#include "audio_synth.h"
#include "dma.h"
#include "fat32.h"
#include "i2s.h"
#include "spi.h"
#include "st7789.h"
#include "visualizer.h"
#include <stdint.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOC_BASE (AHB1PERIPH_BASE + 0x0800UL)
#define PWR_BASE (PERIPH_BASE + 0x00007000UL)
#define FLASH_BASE (AHB1PERIPH_BASE + 0x3C00UL)

/* RCC Registers */
#define RCC_CR (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_PLLI2SCFGR (*(volatile uint32_t *)(RCC_BASE + 0x84))

/* Power and Flash Registers */
#define PWR_CR (*(volatile uint32_t *)(PWR_BASE + 0x00))
#define FLASH_ACR (*(volatile uint32_t *)(FLASH_BASE + 0x00))

/* GPIO Registers */
#define GPIOC_MODER (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_ODR (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

/* RCC Control Register Bits */
#define RCC_CR_HSION (1 << 0)
#define RCC_CR_HSIRDY (1 << 1)
#define RCC_CR_PLLON (1 << 24)
#define RCC_CR_PLLRDY (1 << 25)
#define RCC_CR_PLLI2SON (1 << 26)
#define RCC_CR_PLLI2SRDY (1 << 27)

/**
 * @brief System initialization required by startup code
 * @note Enables FPU for hardware floating point operations
 */
void SystemInit(void) {
  /* Enable FPU: Set CP10 and CP11 to full access */
  (*(volatile uint32_t *)0xE000ED88) |= (0xF << 20);
}

/**
 * @brief Dummy init function to satisfy libc
 */
void _init(void) {}

/**
 * @brief Configure system clocks
 * @details HSI (16MHz) -> PLL -> 96MHz SYSCLK
 *          PLLI2S -> 64MHz for I2S audio
 */
void SystemClock_Config(void) {
  /* Enable power interface clock */
  RCC_APB1ENR |= (1 << 28);

  /* Set voltage regulator scale 1 for max performance */
  PWR_CR |= (3 << 14);

  /* Enable HSI and wait for ready */
  RCC_CR |= RCC_CR_HSION;
  while (!(RCC_CR & RCC_CR_HSIRDY))
    ;

  /* Configure Main PLL: HSI/16 * 192 / 2 = 96MHz */
  RCC_PLLCFGR = 16 | (192 << 6) | (0 << 16) | (0 << 22) | (4 << 24);

  /* Configure PLLI2S: HSI/16 * 192 / 3 = 64MHz for I2S */
  RCC_PLLI2SCFGR = (192 << 6) | (3 << 28);

  /* Enable both PLLs */
  RCC_CR |= RCC_CR_PLLON | RCC_CR_PLLI2SON;

  /* Wait for PLLs to lock with timeout */
  volatile uint32_t timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLRDY) && timeout++ < 10000)
    ;
  timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLI2SRDY) && timeout++ < 10000)
    ;

  /* Configure Flash: 3 wait states, enable caches and prefetch */
  FLASH_ACR = (1 << 8) | (1 << 9) | (1 << 10) | 3;

  /* Set APB1 prescaler to /2 (48MHz max) */
  RCC_CFGR |= (4 << 10);

  /* Switch to PLL as system clock */
  if (RCC_CR & RCC_CR_PLLRDY) {
    RCC_CFGR &= ~3UL;
    RCC_CFGR |= 2UL;
    while ((RCC_CFGR & (3UL << 2)) != (2UL << 2))
      ;
  }
}

/**
 * @brief Main application entry point
 */
int main(void) {
  /* Initialize LED on PC13 for status indication */
  RCC_AHB1ENR |= (1 << 2);
  GPIOC_MODER &= ~(3UL << (13 * 2));
  GPIOC_MODER |= (1UL << (13 * 2));
  GPIOC_ODR &= ~(1UL << 13);

  /* Configure system clocks */
  SystemClock_Config();

  /* Initialize display */
  SPI_Init();
  ST7789_Init();

  /* Initialize SD card and display file list */
  static FAT32_FileEntry files[FAT32_MAX_FILES];
  int file_count = -1;

  if (FAT32_Init() == 0) {
    file_count = FAT32_ListRootFiles(files, FAT32_MAX_FILES);
  }

  /* Display file list (or error) */
  Visualizer_ShowFileList(files, file_count);

  /* Initialize Audio */
  AudioSynth_Init();

  /* Initialize audio hardware */
  int audio_status = I2S_Init();
  if (audio_status == 0) {
    /* Initial fill of buffer to prevent clicking at start */
    AudioSynth_FillBuffer(audio_buffer, AUDIO_BUFFER_SIZE);

    /* Start DMA */
    DMA_Init_I2S(audio_buffer, AUDIO_BUFFER_SIZE);
    I2S_Start();
  }

  while (1) {
    /* Blink LED to show we are alive */
    GPIOC_ODR ^= (1 << 13);
    for (volatile int i = 0; i < 500000; i++)
      ;
  }
}
