#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>

#define ST7789_WIDTH 320
#define ST7789_HEIGHT 240

/* Colors (RGB565) */
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define ORANGE 0xFD20
#define PURPLE 0x8010

void ST7789_Init(void);
void ST7789_Fill(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ST7789_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color,
                     uint16_t bg, uint8_t size);
void ST7789_WriteString(uint16_t x, uint16_t y, const char *str, uint16_t color,
                        uint16_t bg, uint8_t size);
void ST7789_DrawThickFrame(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           uint16_t thickness, uint16_t color);

#endif
