#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

/* SD Card Types */
#define SDCARD_TYPE_UNKNOWN 0
#define SDCARD_TYPE_SD_V1 1
#define SDCARD_TYPE_SD_V2 2
#define SDCARD_TYPE_SDHC 3

/* SD Card Error Codes */
#define SDCARD_OK 0
#define SDCARD_ERROR_INIT -1
#define SDCARD_ERROR_TIMEOUT -2
#define SDCARD_ERROR_READ -3

/**
 * @brief Initialize SD card
 * @return SDCARD_OK on success, error code otherwise
 */
int SDCARD_Init(void);

/**
 * @brief Read a single 512-byte block from SD card
 * @param block_addr Block address (for SDHC) or byte address (for SD)
 * @param buffer Buffer to store 512 bytes
 * @return SDCARD_OK on success, error code otherwise
 */
int SDCARD_ReadBlock(uint32_t block_addr, uint8_t *buffer);

/**
 * @brief Write a single 512-byte block to SD card
 * @param block_addr Block address (for SDHC) or byte address (for SD)
 * @param buffer Buffer containing 512 bytes to write
 * @return SDCARD_OK on success, error code otherwise
 */
int SDCARD_WriteBlock(uint32_t block_addr, const uint8_t *buffer);

/**
 * @brief Get SD card type
 * @return Card type constant
 */
uint8_t SDCARD_GetType(void);

#endif
