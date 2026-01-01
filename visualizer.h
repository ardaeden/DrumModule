#ifndef VISUALIZER_H
#define VISUALIZER_H

#include "fat32.h"
#include <stdint.h>

/**
 * @brief Initialize the visualizer (display "Hello World")
 */
void Visualizer_Init(void);

/**
 * @brief Display file list on screen
 * @param files Array of file entries
 * @param count Number of files
 */
void Visualizer_ShowFileList(FAT32_FileEntry *files, int count);

#endif
