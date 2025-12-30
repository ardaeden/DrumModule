#include "dma.h"
#include "i2s.h"
#include "spi.h"
#include "st7789.h"
#include <math.h>
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

/* Animation Buffer Configuration */
#define BUF_W 32
#define BUF_H 32

/* Global Buffers */
int16_t sine_buf[200];              /* Audio buffer: 100 stereo samples */
uint16_t sprite_buf[BUF_W * BUF_H]; /* Display sprite buffer */

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
 * @brief Simple delay function
 * @param count Delay iterations (calibrated for 96MHz)
 */
void delay(volatile uint32_t count) {
  count *= 6;
  while (count--) {
    __asm("nop");
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
  ST7789_Fill(BLACK);

  /* Generate 440Hz sine wave for audio */
  for (int i = 0; i < 100; i++) {
    float t = (float)i / 100.0f;
    float rad = 6.2831853f * t;
    int16_t val = (int16_t)(30000.0f * sinf(rad));
    sine_buf[i * 2] = val;     /* Left channel */
    sine_buf[i * 2 + 1] = val; /* Right channel */
  }

  /* Initialize audio and display status */
  int audio_status = I2S_Init();

  if (audio_status == 0) {
    DMA_Init_I2S(sine_buf, 200);
    I2S_Start();
    ST7789_WriteString(10, 10, "Audio + Display!", GREEN, BLACK, 2);
  } else {
    ST7789_WriteString(10, 10, "Display Only", YELLOW, BLACK, 2);
  }

  /* Animation state */
  int16_t x = 20, y = 20;
  int16_t dx = 2, dy = 2;
  uint16_t size = 20;

  uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE, WHITE};
  uint8_t color_idx = 0;

  /* Main animation loop */
  while (1) {
    int16_t old_x = x;
    int16_t old_y = y;

    /* Update position */
    x += dx;
    y += dy;

    /* Bounce off edges and change color */
    if (x <= 0) {
      x = 0;
      dx = -dx;
      color_idx = (color_idx + 1) % 8;
    }
    if (x + size >= ST7789_WIDTH) {
      x = ST7789_WIDTH - size;
      dx = -dx;
      color_idx = (color_idx + 1) % 8;
    }
    if (y <= 0) {
      y = 0;
      dy = -dy;
      color_idx = (color_idx + 1) % 8;
    }
    if (y + size >= ST7789_HEIGHT) {
      y = ST7789_HEIGHT - size;
      dy = -dy;
      color_idx = (color_idx + 1) % 8;
    }

    /* Calculate bounding box for efficient redraw */
    int16_t min_x = (old_x < x) ? old_x : x;
    int16_t min_y = (old_y < y) ? old_y : y;
    int16_t max_x = ((old_x + size) > (x + size)) ? (old_x + size) : (x + size);
    int16_t max_y = ((old_y + size) > (y + size)) ? (old_y + size) : (y + size);

    uint16_t update_w = max_x - min_x;
    uint16_t update_h = max_y - min_y;

    /* Clamp to buffer size */
    if (update_w > BUF_W)
      update_w = BUF_W;
    if (update_h > BUF_H)
      update_h = BUF_H;

    /* Clear sprite buffer */
    for (int i = 0; i < update_w * update_h; i++) {
      sprite_buf[i] = BLACK;
    }

    /* Render box into sprite buffer */
    int16_t rel_x = x - min_x;
    int16_t rel_y = y - min_y;
    uint16_t current_color = colors[color_idx];

    for (int py = 0; py < size; py++) {
      for (int px = 0; px < size; px++) {
        int buf_idx = (rel_y + py) * update_w + (rel_x + px);
        if (buf_idx < (update_w * update_h)) {
          sprite_buf[buf_idx] = current_color;
        }
      }
    }

    /* Write sprite to display */
    ST7789_WriteBuffer(min_x, min_y, update_w, update_h, sprite_buf);

    /* Toggle status LED */
    GPIOC_ODR ^= (1UL << 13);

    delay(5000);
  }
}
