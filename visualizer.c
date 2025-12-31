#include "visualizer.h"
#include "st7789.h"

void Visualizer_Init(void) {
  /* Clear screen */
  ST7789_Fill(BLACK);

  /* Draw "Hello World" */
  ST7789_WriteString(40, 100, "Hello World", WHITE, BLACK, 2);
}
