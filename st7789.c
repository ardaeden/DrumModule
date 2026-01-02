#include "st7789.h"
#include "dma_spi.h"
#include "font.h"
#include "spi.h"

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define GPIOA_BASE (AHB1PERIPH_BASE + 0x0000UL)

/* SPI Control Register Bits */
#define SPI_SR_BSY (1 << 7)

/* GPIOA Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_BSRR (*(volatile uint32_t *)(GPIOA_BASE + 0x18))

/* ST7789 Control Pins */
#define PIN_CS 4  /* Chip Select */
#define PIN_DC 2  /* Data/Command */
#define PIN_RES 3 /* Reset */
#define PIN_BLK 1 /* Backlight */

/* GPIO Control Macros */
#define CS_LOW() (GPIOA_BSRR = (1 << (PIN_CS + 16)))
#define CS_HIGH() (GPIOA_BSRR = (1 << PIN_CS))
#define DC_CMD() (GPIOA_BSRR = (1 << (PIN_DC + 16)))
#define DC_DATA() (GPIOA_BSRR = (1 << PIN_DC))
#define RES_LOW() (GPIOA_BSRR = (1 << (PIN_RES + 16)))
#define RES_HIGH() (GPIOA_BSRR = (1 << PIN_RES))
#define BLK_LOW() (GPIOA_BSRR = (1 << (PIN_BLK + 16)))
#define BLK_HIGH() (GPIOA_BSRR = (1 << PIN_BLK))

/**
 * @brief Simple delay for display timing
 * @param count Number of iterations
 */
static void ST7789_Delay(volatile uint32_t count) {
  while (count--) {
    __asm("nop");
  }
}

/**
 * @brief Send command to ST7789
 * @param cmd Command byte
 */
static void ST7789_WriteCommand(uint8_t cmd) {
  DC_CMD();
  CS_LOW();
  SPI_Transmit(cmd);
  CS_HIGH();
}

/**
 * @brief Send data to ST7789
 * @param data Data byte
 */
static void ST7789_WriteData(uint8_t data) {
  DC_DATA();
  CS_LOW();
  SPI_Transmit(data);
  CS_HIGH();
}

/**
 * @brief Initialize GPIO pins for ST7789 control
 */
void ST7789_InitGPIO(void) {
  /* Configure PA1, PA2, PA3, PA4 as outputs */
  uint32_t moder = GPIOA_MODER;
  moder &= ~((3UL << 2) | (3UL << 4) | (3UL << 6) | (3UL << 8));
  moder |= ((1UL << 2) | (1UL << 4) | (1UL << 6) | (1UL << 8));
  GPIOA_MODER = moder;

  /* Set initial states */
  CS_HIGH();
  RES_HIGH();
  BLK_LOW();
}

/**
 * @brief Initialize ST7789 display
 * @details Configures display for 16-bit color, landscape orientation
 */
void ST7789_Init(void) {
  ST7789_InitGPIO();

  /* Hardware reset sequence */
  RES_LOW();
  ST7789_Delay(100000);
  RES_HIGH();
  ST7789_Delay(100000);

  /* Software reset */
  ST7789_WriteCommand(0x01);
  ST7789_Delay(150000);

  /* Sleep out */
  ST7789_WriteCommand(0x11);
  ST7789_Delay(50000);

  /* Color mode: 16-bit (RGB565) */
  ST7789_WriteCommand(0x3A);
  ST7789_WriteData(0x55);

  /* Memory data access control: Landscape orientation */
  ST7789_WriteCommand(0x36);
  ST7789_WriteData(0x70);

  /* Inversion on (typical for IPS panels) */
  ST7789_WriteCommand(0x21);

  /* Normal display mode */
  ST7789_WriteCommand(0x13);

  /* Display on */
  ST7789_WriteCommand(0x29);

  /* Enable backlight */
  BLK_HIGH();

  /* Initialize DMA2 for fast fills */
  SPI_DMA_Init();
}

/**
 * @brief Set drawing window on display
 * @param x0 Start column
 * @param y0 Start row
 * @param x1 End column
 * @param y1 End row
 */
void ST7789_SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1,
                             uint16_t y1) {
  /* Column address set */
  ST7789_WriteCommand(0x2A);
  ST7789_WriteData(x0 >> 8);
  ST7789_WriteData(x0 & 0xFF);
  ST7789_WriteData(x1 >> 8);
  ST7789_WriteData(x1 & 0xFF);

  /* Row address set */
  ST7789_WriteCommand(0x2B);
  ST7789_WriteData(y0 >> 8);
  ST7789_WriteData(y0 & 0xFF);
  ST7789_WriteData(y1 >> 8);
  ST7789_WriteData(y1 & 0xFF);

  /* Memory write */
  ST7789_WriteCommand(0x2C);
}

/**
 * @brief Fill entire screen with solid color
 * @param color RGB565 color value
 */
void ST7789_Fill(uint16_t color) {
  ST7789_SetAddressWindow(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);

  DC_DATA();
  CS_LOW();

  /* Use direct SPI writes instead of DMA to avoid grain artifacts */
  SPI_SetDataSize16();
  uint32_t pixels = ST7789_WIDTH * ST7789_HEIGHT;
  for (uint32_t i = 0; i < pixels; i++) {
    SPI_WriteData16(color);
  }
  SPI_WaitBusy();
  SPI_SetDataSize8();

  CS_HIGH();
}

/**
 * @brief Fill rectangle with solid color
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param color RGB565 color value
 */
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t color) {
  if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    return;
  if ((x + w - 1) >= ST7789_WIDTH)
    w = ST7789_WIDTH - x;
  if ((y + h - 1) >= ST7789_HEIGHT)
    h = ST7789_HEIGHT - y;

  ST7789_SetAddressWindow(x, y, x + w - 1, y + h - 1);

  DC_DATA();
  CS_LOW();

  uint32_t pixels = w * h;

  /* Use DMA2 for small fills (> 20 pixels) to accelerate frames */
  if (pixels > 20) {
    SPI_SetDataSize16();
    SPI_DMA_FillColor(color, pixels);
    SPI_SetDataSize8();
  } else {
    /* Use direct 16-bit mode for small fills */
    SPI_SetDataSize16();
    for (uint32_t i = 0; i < pixels; i++) {
      SPI_WriteData16(color);
    }
    SPI_WaitBusy();
    SPI_SetDataSize8();
  }

  CS_HIGH();
}

/**
 * @brief Draw single pixel
 * @param x X coordinate
 * @param y Y coordinate
 * @param color RGB565 color value
 */
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color) {
  if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    return;

  ST7789_SetAddressWindow(x, y, x, y);

  DC_DATA();
  CS_LOW();

  SPI_Transmit(color >> 8);
  SPI_Transmit(color & 0xFF);

  CS_HIGH();
}

/**
 * @brief Draw character
 * @param x X coordinate
 * @param y Y coordinate
 * @param c Character to draw
 * @param color Foreground color
 * @param bg Background color
 * @param size Scale factor (1 = 5x7 pixels)
 */
void ST7789_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color,
                     uint16_t bg, uint8_t size) {
  if ((x >= ST7789_WIDTH) || (y >= ST7789_HEIGHT))
    return;
  if (c < 32 || c > 126)
    return;

  uint8_t char_index = c - 32;

  for (int i = 0; i < 5; i++) {
    uint8_t line = font_default[char_index][i];
    for (int j = 0; j < 7; j++) {
      if (line & (1 << j)) {
        if (size == 1) {
          ST7789_DrawPixel(x + i, y + j, color);
        } else {
          ST7789_SetAddressWindow(x + (i * size), y + (j * size),
                                  x + (i * size) + size - 1,
                                  y + (j * size) + size - 1);
          DC_DATA();
          CS_LOW();
          uint8_t color_hi = color >> 8;
          uint8_t color_lo = color & 0xFF;
          for (int k = 0; k < size * size; k++) {
            SPI_WriteData8(color_hi);
            SPI_WriteData8(color_lo);
          }
          SPI_WaitBusy();
          CS_HIGH();
        }
      } else if (bg != color) {
        if (size == 1) {
          ST7789_DrawPixel(x + i, y + j, bg);
        } else {
          ST7789_SetAddressWindow(x + (i * size), y + (j * size),
                                  x + (i * size) + size - 1,
                                  y + (j * size) + size - 1);
          DC_DATA();
          CS_LOW();
          uint8_t color_hi = bg >> 8;
          uint8_t color_lo = bg & 0xFF;
          for (int k = 0; k < size * size; k++) {
            SPI_WriteData8(color_hi);
            SPI_WriteData8(color_lo);
          }
          SPI_WaitBusy();
          CS_HIGH();
        }
      }
    }
  }
}

/**
 * @brief Draw text string
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param color Foreground color
 * @param bg Background color
 * @param size Scale factor
 */
/**
 * @brief Draw a thick frame using Quad-Burst DMA
 * @param x Top left X
 * @param y Top left Y
 * @param w Width
 * @param h Height
 * @param thickness Border thickness
 * @param color Frame color
 */
void ST7789_DrawThickFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           uint16_t thickness, uint16_t color) {
  if (thickness == 0)
    return;

  /* Top segment */
  ST7789_FillRect(x, y, w, thickness, color);

  /* Bottom segment */
  ST7789_FillRect(x, y + h - thickness, w, thickness, color);

  /* Left segment (middle) */
  ST7789_FillRect(x, y + thickness, thickness, h - 2 * thickness, color);

  /* Right segment (middle) */
  ST7789_FillRect(x + w - thickness, y + thickness, thickness,
                  h - 2 * thickness, color);
}
void ST7789_WriteString(uint16_t x, uint16_t y, const char *str, uint16_t color,
                        uint16_t bg, uint8_t size) {
  while (*str) {
    if (x + (5 * size) >= ST7789_WIDTH) {
      x = 0;
      y += (8 * size);
      if (y >= ST7789_HEIGHT)
        break;
    }
    ST7789_DrawChar(x, y, *str++, color, bg, size);
    x += (6 * size);
  }
}
