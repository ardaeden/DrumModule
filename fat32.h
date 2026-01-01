#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

#define FAT32_MAX_FILES 32
#define FAT32_FILENAME_LEN 13 /* 8.3 format + null terminator */

/**
 * @brief File entry structure
 */
typedef struct {
  char name[FAT32_FILENAME_LEN];
  uint32_t size;
  uint8_t is_dir;
} FAT32_FileEntry;

/**
 * @brief Initialize FAT32 filesystem
 * @return 0 on success, -1 on error
 */
int FAT32_Init(void);

/**
 * @brief Get list of files in root directory
 * @param files Array to store file entries
 * @param max_files Maximum number of files to read
 * @return Number of files found, or -1 on error
 */
int FAT32_ListRootFiles(FAT32_FileEntry *files, int max_files);

#endif
