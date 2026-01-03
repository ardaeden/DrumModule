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
  uint32_t first_cluster;
  uint8_t is_dir;
} FAT32_FileEntry;

/**
 * @brief Initialize FAT32 filesystem
 * @return 0 on success, -1 on error
 */
int FAT32_Init(void);

/**
 * @brief Get root directory cluster number
 * @return Root cluster number
 */
uint32_t FAT32_GetRootCluster(void);

/**
 * @brief Get list of files in a directory
 * @param cluster Directory cluster (use FAT32_GetRootCluster() for root)
 * @param files Array to store file entries
 * @param max_files Maximum number of files to read
 * @return Number of files found, or -1 on error
 */
int FAT32_ListDir(uint32_t cluster, FAT32_FileEntry *files, int max_files);

/**
 * @brief Get first sector of a file
 * @param file File entry
 * @return First sector number
 */
uint32_t FAT32_GetFileSector(FAT32_FileEntry *file);

#endif
