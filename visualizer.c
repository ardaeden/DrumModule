#include "visualizer.h"
#include "fat32.h"
#include "st7789.h"
#include <stdio.h>

void Visualizer_Init(void) {
  /* Clear screen */
  ST7789_Fill(BLACK);

  /* Draw "Hello World" */
  ST7789_WriteString(40, 100, "Hello World", WHITE, BLACK, 2);
}

void Visualizer_ShowFileList(FAT32_FileEntry *files, int count) {
  char line_buf[32];

  /* Clear screen */
  ST7789_Fill(BLACK);

  /* Draw title */
  ST7789_WriteString(10, 10, "SD Card Files:", CYAN, BLACK, 2);

  if (count < 0) {
    /* Error reading SD card */
    ST7789_WriteString(10, 40, "SD Card Error!", RED, BLACK, 2);
    ST7789_WriteString(10, 70, "Check card/format", YELLOW, BLACK, 1);
    return;
  }

  if (count == 0) {
    /* No files found */
    ST7789_WriteString(10, 40, "No files found", YELLOW, BLACK, 2);
    ST7789_WriteString(10, 70, "Card OK, root empty", WHITE, BLACK, 1);
    return;
  }

  /* Display files (up to 10 visible) */
  int y = 40;
  for (int i = 0; i < count && i < 10; i++) {
    /* Format: filename (size bytes) [DIR] */
    if (files[i].is_dir) {
      snprintf(line_buf, sizeof(line_buf), "[DIR] %s", files[i].name);
      ST7789_WriteString(10, y, line_buf, GREEN, BLACK, 1);
    } else {
      snprintf(line_buf, sizeof(line_buf), "%s (%lu)", files[i].name,
               (unsigned long)files[i].size);
      ST7789_WriteString(10, y, line_buf, WHITE, BLACK, 1);
    }
    y += 16;
  }

  /* Show count if more files exist */
  if (count > 10) {
    snprintf(line_buf, sizeof(line_buf), "... +%d more", count - 10);
    ST7789_WriteString(10, y, line_buf, YELLOW, BLACK, 1);
  }
}
