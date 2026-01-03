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
 * @brief Find a directory by name in a given directory
 * @param parent_cluster Cluster of the directory to search in
 * @param name Name of the directory to find
 * @return First cluster of the directory if found, 0 otherwise
 */
uint32_t FAT32_FindDir(uint32_t parent_cluster, const char *name);

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

/**
 * @brief Check if a file exists
 * @param dir_cluster Directory cluster to search in
 * @param filename Filename to check (8.3 format)
 * @return 1 if exists, 0 if not
 */
int FAT32_FileExists(uint32_t dir_cluster, const char *filename);

/**
 * @brief Write data to a file (create or overwrite)
 * @param dir_cluster Directory cluster where file should be created
 * @param filename Filename (8.3 format, e.g., "KIT-001.DRM")
 * @param data Data to write
 * @param size Size of data in bytes
 * @return 0 on success, -1 on error
 */
int FAT32_WriteFile(uint32_t dir_cluster, const char *filename,
                    const uint8_t *data, uint32_t size);

#endif
