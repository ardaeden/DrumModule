#include "fat32.h"
#include "sdcard.h"
#include <stdint.h>
#include <string.h>

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

int FAT32_Init(void) {
  uint32_t partition_start = 0;

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
      partition_start = read_u32(sector_buffer, 454);
    }
  }

  /* Read boot sector (either sector 0 or partition start) */
  if (SDCARD_ReadBlock(partition_start, sector_buffer) != SDCARD_OK) {
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
      partition_start + reserved_sectors + (num_fats * fat_size);

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

int FAT32_ListRootFiles(FAT32_FileEntry *files, int max_files) {
  int file_count = 0;
  uint32_t sector = cluster_to_sector(root_cluster);

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

      /* Skip long filename entries, volume labels, and system files */
      if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME) {
        continue;
      }
      if (attr & ATTR_VOLUME_ID) {
        continue;
      }

      /* Copy filename */
      copy_filename(files[file_count].name, dir_entry + DIR_NAME);

      /* Get file size */
      files[file_count].size = read_u32(dir_entry, DIR_FILE_SIZE);

      /* Check if directory */
      files[file_count].is_dir = (attr & ATTR_DIRECTORY) ? 1 : 0;

      file_count++;
    }
  }

  return file_count;
}
