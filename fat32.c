#define SECTOR_SIZE 512

#include "fat32.h"
#include "sdcard.h"
#include <stdint.h>
#include <string.h>
#include <strings.h>

/* FAT32 Boot Sector Offsets */
#define BS_BYTES_PER_SEC 11
#define BS_SEC_PER_CLUS 13
#define BS_RSVD_SEC_CNT 14
#define BS_NUM_FATS 16
#define BS_FAT_SZ_32 36
#define BS_ROOT_CLUS 44

/* Directory Entry Offsets */
#define DIR_NAME 0
#define DIR_ATTR 11
#define DIR_FSTCLUS_HI 20
#define DIR_FSTCLUS_LO 26
#define DIR_FILE_SIZE 28

/* Attributes */
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME                                                         \
  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

static uint8_t sector_buffer[512];
static uint16_t bytes_per_sector;
static uint8_t sectors_per_cluster;
static uint32_t reserved_sectors;
static uint32_t fat_size;
static uint32_t root_cluster;
static uint32_t first_data_sector;
static uint32_t partition_start_lba;

/**
 * @brief Read 16-bit little-endian value
 */
static uint16_t read_u16(uint8_t *buf, uint16_t offset) {
  return buf[offset] | (buf[offset + 1] << 8);
}

/**
 * @brief Read 32-bit little-endian value
 */
static uint32_t read_u32(uint8_t *buf, uint16_t offset) {
  return buf[offset] | (buf[offset + 1] << 8) | (buf[offset + 2] << 16) |
         (buf[offset + 3] << 24);
}

/**
 * @brief Convert cluster number to sector number
 */
static uint32_t cluster_to_sector(uint32_t cluster) {
  return first_data_sector + (cluster - 2) * sectors_per_cluster;
}

/**
 * @brief Write 16-bit little-endian value
 */
static void write_u16(uint8_t *buf, uint16_t offset, uint16_t value) {
  buf[offset] = value & 0xFF;
  buf[offset + 1] = (value >> 8) & 0xFF;
}

/**
 * @brief Write 32-bit little-endian value
 */
static void write_u32(uint8_t *buf, uint16_t offset, uint32_t value) {
  buf[offset] = value & 0xFF;
  buf[offset + 1] = (value >> 8) & 0xFF;
  buf[offset + 2] = (value >> 16) & 0xFF;
  buf[offset + 3] = (value >> 24) & 0xFF;
}

int FAT32_Init(void) {
  partition_start_lba = 0;

  /* Initialize SD card */
  if (SDCARD_Init() != SDCARD_OK) {
    return -1;
  }

  /* Read MBR (sector 0) to find partition */
  if (SDCARD_ReadBlock(0, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  /* Check for MBR signature (0x55AA at offset 510) */
  if (sector_buffer[510] == 0x55 && sector_buffer[511] == 0xAA) {
    /* Read first partition entry (offset 446) */
    /* Partition type at offset 450 (0x0B = FAT32, 0x0C = FAT32 LBA) */
    uint8_t partition_type = sector_buffer[450];

    if (partition_type == 0x0B || partition_type == 0x0C ||
        partition_type == 0x04 || partition_type == 0x06) {
      /* Get partition start sector (LBA) at offset 454 */
      partition_start_lba = read_u32(sector_buffer, 454);
    }
  }

  /* Read boot sector (either sector 0 or partition start) */
  if (SDCARD_ReadBlock(partition_start_lba, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  /* Parse boot sector */
  bytes_per_sector = read_u16(sector_buffer, BS_BYTES_PER_SEC);
  sectors_per_cluster = sector_buffer[BS_SEC_PER_CLUS];
  reserved_sectors = read_u16(sector_buffer, BS_RSVD_SEC_CNT);
  uint8_t num_fats = sector_buffer[BS_NUM_FATS];
  fat_size = read_u32(sector_buffer, BS_FAT_SZ_32);
  root_cluster = read_u32(sector_buffer, BS_ROOT_CLUS);

  /* Calculate first data sector (relative to partition start) */
  first_data_sector =
      partition_start_lba + reserved_sectors + (num_fats * fat_size);

  return 0;
}

/**
 * @brief Copy and clean 8.3 filename
 */
static void copy_filename(char *dest, uint8_t *src) {
  int i, j = 0;

  /* Copy name (8 chars) */
  for (i = 0; i < 8 && src[i] != ' '; i++) {
    dest[j++] = src[i];
  }

  /* Add dot if extension exists */
  if (src[8] != ' ') {
    dest[j++] = '.';
    /* Copy extension (3 chars) */
    for (i = 8; i < 11 && src[i] != ' '; i++) {
      dest[j++] = src[i];
    }
  }

  dest[j] = '\0';
}

/**
 * @brief Find and allocate a free cluster in FAT
 */
static uint32_t allocate_free_cluster(void) {
  uint32_t fat_sector = partition_start_lba + reserved_sectors;

  /* Scan FAT sectors (up to 200 sectors to cover larger cards) */
  for (int s = 0; s < 200; s++) {
    if (SDCARD_ReadBlock(fat_sector + s, sector_buffer) != SDCARD_OK) {
      return 0;
    }

    for (int i = 0; i < 128; i++) {
      uint32_t entry = read_u32(sector_buffer, i * 4) & 0x0FFFFFFF;
      if (entry == 0x00000000) {
        /* Found free cluster */
        uint32_t cluster = (s * 128) + i;
        if (cluster < 2)
          continue;

        /* Mark as EOF (End of Chain) */
        write_u32(sector_buffer, i * 4, 0x0FFFFFFF);
        if (SDCARD_WriteBlock(fat_sector + s, sector_buffer) != SDCARD_OK) {
          return 0;
        }

        return cluster;
      }
    }
  }
  return 0;
}

uint32_t FAT32_GetRootCluster(void) { return root_cluster; }

uint32_t FAT32_FindDir(uint32_t parent_cluster, const char *name) {
  uint32_t sector = cluster_to_sector(parent_cluster);

  for (int sec = 0; sec < sectors_per_cluster; sec++) {
    if (SDCARD_ReadBlock(sector + sec, sector_buffer) != SDCARD_OK) {
      return 0;
    }

    /* Parse directory entries (32 bytes each) */
    for (int entry = 0; entry < 16; entry++) {
      uint8_t *dir_entry = sector_buffer + (entry * 32);

      /* Check if entry is free */
      if (dir_entry[DIR_NAME] == 0x00) {
        return 0;
      }

      if (dir_entry[DIR_NAME] == 0xE5) {
        continue;
      }

      uint8_t attr = dir_entry[DIR_ATTR];

      /* Skip long names, etc */
      if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME ||
          (attr & ATTR_VOLUME_ID)) {
        continue;
      }

      /* Only interested in directories */
      if (!(attr & ATTR_DIRECTORY)) {
        continue;
      }

      char entry_name[FAT32_FILENAME_LEN];
      copy_filename(entry_name, dir_entry + DIR_NAME);

      if (strcasecmp(entry_name, name) == 0) {
        /* Match found! */
        uint16_t cluster_hi = read_u16(dir_entry, DIR_FSTCLUS_HI);
        uint16_t cluster_lo = read_u16(dir_entry, DIR_FSTCLUS_LO);
        return ((uint32_t)cluster_hi << 16) | cluster_lo;
      }
    }
  }

  return 0;
}

int FAT32_ListDir(uint32_t cluster, FAT32_FileEntry *files, int max_files) {
  int file_count = 0;
  uint32_t sector = cluster_to_sector(cluster);

  /* Read root directory sectors */
  for (int sec = 0; sec < sectors_per_cluster && file_count < max_files;
       sec++) {
    if (SDCARD_ReadBlock(sector + sec, sector_buffer) != SDCARD_OK) {
      return -1;
    }

    /* Parse directory entries (32 bytes each) */
    for (int entry = 0; entry < 16 && file_count < max_files; entry++) {
      uint8_t *dir_entry = sector_buffer + (entry * 32);

      /* Check if entry is free */
      if (dir_entry[DIR_NAME] == 0x00) {
        /* End of directory */
        return file_count;
      }

      if (dir_entry[DIR_NAME] == 0xE5) {
        /* Deleted entry, skip */
        continue;
      }

      uint8_t attr = dir_entry[DIR_ATTR];

      /* Skip long filename entries, volume labels, hidden, and system files */
      if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
        continue;
      }
      if (attr & (ATTR_VOLUME_ID | ATTR_HIDDEN | ATTR_SYSTEM)) {
        continue;
      }

      /* Copy filename */
      copy_filename(files[file_count].name, dir_entry + DIR_NAME);

      /* Get file size */
      files[file_count].size = read_u32(dir_entry, DIR_FILE_SIZE);

      /* Get first cluster */
      uint16_t cluster_hi = read_u16(dir_entry, DIR_FSTCLUS_HI);
      uint16_t cluster_lo = read_u16(dir_entry, DIR_FSTCLUS_LO);
      files[file_count].first_cluster =
          ((uint32_t)cluster_hi << 16) | cluster_lo;

      /* Check if directory */
      files[file_count].is_dir = (attr & ATTR_DIRECTORY) ? 1 : 0;

      file_count++;
    }
  }

  return file_count;
}

uint32_t FAT32_GetFileSector(FAT32_FileEntry *file) {
  if (file->first_cluster == 0) {
    return 0;
  }
  return cluster_to_sector(file->first_cluster);
}

int FAT32_FileExists(uint32_t dir_cluster, const char *filename) {
  uint32_t sector = cluster_to_sector(dir_cluster);

  for (int sec = 0; sec < sectors_per_cluster; sec++) {
    if (SDCARD_ReadBlock(sector + sec, sector_buffer) != SDCARD_OK) {
      return 0;
    }

    for (int entry = 0; entry < 16; entry++) {
      uint8_t *dir_entry = sector_buffer + (entry * 32);

      if (dir_entry[DIR_NAME] == 0x00) {
        return 0;
      }

      if (dir_entry[DIR_NAME] == 0xE5) {
        continue;
      }

      uint8_t attr = dir_entry[DIR_ATTR];
      if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
        continue;
      }

      char entry_name[FAT32_FILENAME_LEN];
      copy_filename(entry_name, dir_entry + DIR_NAME);

      if (strcasecmp(entry_name, filename) == 0) {
        return 1;
      }
    }
  }

  return 0;
}

int FAT32_WriteFile(uint32_t dir_cluster, const char *filename,
                    const uint8_t *data, uint32_t size) {
  // Simplified implementation for small files (<512 bytes)
  if (size > 512) {
    return -1;
  }

  uint32_t dir_sector = cluster_to_sector(dir_cluster);
  int found_entry = -1;
  int found_sector = -1;
  int empty_entry = -1;
  int empty_sector = -1;

  // Search for existing file or empty slot
  for (int sec = 0; sec < sectors_per_cluster && found_entry == -1; sec++) {
    if (SDCARD_ReadBlock(dir_sector + sec, sector_buffer) != SDCARD_OK) {
      return -1;
    }

    for (int entry = 0; entry < 16; entry++) {
      uint8_t *dir_entry = sector_buffer + (entry * 32);

      if (dir_entry[DIR_NAME] == 0x00 || dir_entry[DIR_NAME] == 0xE5) {
        if (empty_entry == -1) {
          empty_entry = entry;
          empty_sector = sec;
        }
        if (dir_entry[DIR_NAME] == 0x00) {
          break;
        }
        continue;
      }

      uint8_t attr = dir_entry[DIR_ATTR];
      if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
        continue;
      }

      char entry_name[FAT32_FILENAME_LEN];
      copy_filename(entry_name, dir_entry + DIR_NAME);

      if (strcasecmp(entry_name, filename) == 0) {
        found_entry = entry;
        found_sector = sec;
        break;
      }
    }
  }

  int use_entry = (found_entry != -1) ? found_entry : empty_entry;
  int use_sector = (found_entry != -1) ? found_sector : empty_sector;

  if (use_entry == -1) {
    return -1;
  }

  // Read directory sector
  if (SDCARD_ReadBlock(dir_sector + use_sector, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  uint8_t *dir_entry = sector_buffer + (use_entry * 32);

  // If new entry, clear it first
  if (found_entry == -1) {
    memset(dir_entry, 0, 32);

    // Also mark next entry as 0x00 if we used a 0x00 entry
    // to maintain end-of-directory marker if possible,
    // but in simple implementation we just write the entry.
  }

  // Parse filename into 8.3 format
  char name_part[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  char ext_part[4] = {' ', ' ', ' ', '\0'};

  const char *dot = strchr(filename, '.');
  int name_len = dot ? (dot - filename) : strlen(filename);
  if (name_len > 8)
    name_len = 8;

  for (int i = 0; i < name_len; i++) {
    name_part[i] = filename[i];
  }

  if (dot) {
    int ext_len = strlen(dot + 1);
    if (ext_len > 3)
      ext_len = 3;
    for (int i = 0; i < ext_len; i++) {
      ext_part[i] = dot[1 + i];
    }
  }

  // Write directory entry
  for (int i = 0; i < 8; i++) {
    dir_entry[DIR_NAME + i] = name_part[i];
  }
  for (int i = 0; i < 3; i++) {
    dir_entry[DIR_NAME + 8 + i] = ext_part[i];
  }

  dir_entry[DIR_ATTR] = ATTR_ARCHIVE;
  write_u32(dir_entry, DIR_FILE_SIZE, size);

  uint32_t file_cluster = 0;
  if (found_entry != -1) {
    /* Reuse existing cluster */
    uint16_t cluster_hi = read_u16(dir_entry, DIR_FSTCLUS_HI);
    uint16_t cluster_lo = read_u16(dir_entry, DIR_FSTCLUS_LO);
    file_cluster = ((uint32_t)cluster_hi << 16) | cluster_lo;
  } else {
    /* Allocate a new cluster for new file */
    file_cluster = allocate_free_cluster();
    if (file_cluster >= 2) {
      write_u16(dir_entry, DIR_FSTCLUS_HI, (file_cluster >> 16) & 0xFFFF);
      write_u16(dir_entry, DIR_FSTCLUS_LO, file_cluster & 0xFFFF);
    }
  }

  if (file_cluster < 2) {
    return -1;
  }

  // Write updated directory
  if (SDCARD_WriteBlock(dir_sector + use_sector, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  // Write file data
  uint32_t data_sector = cluster_to_sector(file_cluster);
  uint8_t data_buffer[512] = {0};

  for (uint32_t i = 0; i < size && i < 512; i++) {
    data_buffer[i] = data[i];
  }

  if (SDCARD_WriteBlock(data_sector, data_buffer) != SDCARD_OK) {
    return -1;
  }

  return 0;
}
