#include "pattern_manager.h"
#include "fat32.h"
#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

int Pattern_Save(Pattern *pattern, uint8_t slot) {
  if (slot < 1 || slot > 100)
    return -1;

  // Find PATTERNS directory
  uint32_t patterns_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "PATTERNS");
  if (patterns_cluster == 0) {
    return -1; // Directory must exist
  }

  // Generate filename: PAT-XXX.PAT
  char filename[13];
  snprintf(filename, sizeof(filename), "PAT-%03d.PAT", slot);

  // Write binary data
  return FAT32_WriteFile(patterns_cluster, filename, (uint8_t *)pattern,
                         sizeof(Pattern));
}

int Pattern_Load(Pattern *pattern, uint8_t slot) {
  if (slot < 1 || slot > 100)
    return -1;

  // Find PATTERNS directory
  uint32_t patterns_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "PATTERNS");
  if (patterns_cluster == 0)
    return -1;

  // Generate filename
  char filename[13];
  snprintf(filename, sizeof(filename), "PAT-%03d.PAT", slot);

  // Check if file exists
  if (!FAT32_FileExists(patterns_cluster, filename))
    return -1;

  // Find the file entry
  FAT32_FileEntry files[FAT32_MAX_FILES];
  int count = FAT32_ListDir(patterns_cluster, files, FAT32_MAX_FILES);

  FAT32_FileEntry *target_file = NULL;
  for (int i = 0; i < count; i++) {
    if (strcasecmp(files[i].name, filename) == 0) {
      target_file = &files[i];
      break;
    }
  }

  if (!target_file)
    return -1;

  // Read first sector
  uint32_t sector = FAT32_GetFileSector(target_file);
  uint8_t sector_buffer[512];

  if (SDCARD_ReadBlock(sector, sector_buffer) != SDCARD_OK)
    return -1;

  // Copy binary data to pattern struct
  if (target_file->size > sizeof(Pattern)) {
    // File is larger than expected, but we only need sizeof(Pattern)
  }

  memcpy(pattern, sector_buffer, sizeof(Pattern));

  return 0;
}

int Pattern_GetOccupiedSlots(uint8_t *slots, int max_slots) {
  uint32_t patterns_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "PATTERNS");
  if (patterns_cluster == 0)
    return 0;

  FAT32_FileEntry files[FAT32_MAX_FILES];
  int count = FAT32_ListDir(patterns_cluster, files, FAT32_MAX_FILES);

  int occupied_count = 0;
  for (int i = 0; i < count && occupied_count < max_slots; i++) {
    if (strncmp(files[i].name, "PAT-", 4) == 0) {
      int slot_num;
      if (sscanf(files[i].name + 4, "%d", &slot_num) == 1) {
        if (slot_num >= 1 && slot_num <= 100) {
          slots[occupied_count++] = slot_num;
        }
      }
    }
  }

  return occupied_count;
}
