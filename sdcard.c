#include "sdcard.h"
#include "sdcard_spi.h"
#include <stdint.h>

/* SD Card Commands */
#define CMD0 0    /* GO_IDLE_STATE */
#define CMD8 8    /* SEND_IF_COND */
#define CMD17 17  /* READ_SINGLE_BLOCK */
#define CMD55 55  /* APP_CMD */
#define CMD58 58  /* READ_OCR */
#define ACMD41 41 /* SD_SEND_OP_COND */

/* Response Tokens */
#define R1_IDLE_STATE 0x01
#define R1_READY 0x00
#define DATA_START_TOKEN 0xFE

static uint8_t card_type = SDCARD_TYPE_UNKNOWN;

/**
 * @brief Send a command to SD card
 * @param cmd Command index
 * @param arg Command argument
 * @return R1 response
 */
static uint8_t SDCARD_SendCommand(uint8_t cmd, uint32_t arg) {
  uint8_t response;
  uint8_t retry = 0;

  /* Send command packet */
  SDCARD_SPI_TransmitReceive(0x40 | cmd); /* Start + command */
  SDCARD_SPI_TransmitReceive((arg >> 24) & 0xFF);
  SDCARD_SPI_TransmitReceive((arg >> 16) & 0xFF);
  SDCARD_SPI_TransmitReceive((arg >> 8) & 0xFF);
  SDCARD_SPI_TransmitReceive(arg & 0xFF);

  /* CRC (only matters for CMD0 and CMD8) */
  if (cmd == CMD0)
    SDCARD_SPI_TransmitReceive(0x95);
  else if (cmd == CMD8)
    SDCARD_SPI_TransmitReceive(0x87);
  else
    SDCARD_SPI_TransmitReceive(0xFF);

  /* Wait for response (not 0xFF) */
  do {
    response = SDCARD_SPI_TransmitReceive(0xFF);
    retry++;
  } while ((response == 0xFF) && (retry < 10));

  return response;
}

/**
 * @brief Send application-specific command
 */
static uint8_t SDCARD_SendAppCommand(uint8_t cmd, uint32_t arg) {
  SDCARD_SendCommand(CMD55, 0);
  return SDCARD_SendCommand(cmd, arg);
}

int SDCARD_Init(void) {
  uint8_t response;
  uint16_t timeout;

  /* Initialize SPI3 */
  SDCARD_SPI_Init();

  /* Set slow speed for initialization */
  SDCARD_SPI_SetSlowSpeed();

  /* Send 80+ clock pulses with CS high */
  SDCARD_SPI_CS_High();
  for (int i = 0; i < 10; i++) {
    SDCARD_SPI_TransmitReceive(0xFF);
  }

  /* Select card */
  SDCARD_SPI_CS_Low();

  /* CMD0: Reset card to idle state */
  response = SDCARD_SendCommand(CMD0, 0);
  if (response != R1_IDLE_STATE) {
    SDCARD_SPI_CS_High();
    return SDCARD_ERROR_INIT;
  }

  /* CMD8: Check voltage range (SD v2) */
  response = SDCARD_SendCommand(CMD8, 0x1AA);
  if (response == R1_IDLE_STATE) {
    /* SD v2 card */
    /* Read 4-byte response */
    for (int i = 0; i < 4; i++) {
      SDCARD_SPI_TransmitReceive(0xFF);
    }

    /* ACMD41: Initialize card */
    timeout = 0xFFFF;
    do {
      response = SDCARD_SendAppCommand(ACMD41, 0x40000000);
      timeout--;
    } while ((response != R1_READY) && (timeout > 0));

    if (timeout == 0) {
      SDCARD_SPI_CS_High();
      return SDCARD_ERROR_TIMEOUT;
    }

    /* CMD58: Read OCR to check if SDHC */
    response = SDCARD_SendCommand(CMD58, 0);
    if (response == R1_READY) {
      uint8_t ocr[4];
      for (int i = 0; i < 4; i++) {
        ocr[i] = SDCARD_SPI_TransmitReceive(0xFF);
      }
      /* Check CCS bit (bit 30) */
      if (ocr[0] & 0x40) {
        card_type = SDCARD_TYPE_SDHC;
      } else {
        card_type = SDCARD_TYPE_SD_V2;
      }
    }
  } else {
    /* SD v1 or MMC */
    timeout = 0xFFFF;
    do {
      response = SDCARD_SendAppCommand(ACMD41, 0);
      timeout--;
    } while ((response != R1_READY) && (timeout > 0));

    if (timeout == 0) {
      SDCARD_SPI_CS_High();
      return SDCARD_ERROR_TIMEOUT;
    }

    card_type = SDCARD_TYPE_SD_V1;
  }

  /* Deselect card */
  SDCARD_SPI_CS_High();
  SDCARD_SPI_TransmitReceive(0xFF);

  /* Switch to fast speed */
  SDCARD_SPI_SetFastSpeed();

  return SDCARD_OK;
}

int SDCARD_ReadBlock(uint32_t block_addr, uint8_t *buffer) {
  uint8_t response;
  uint16_t timeout;

  /* For non-SDHC cards, convert block address to byte address */
  if (card_type != SDCARD_TYPE_SDHC) {
    block_addr *= 512;
  }

  /* Select card */
  SDCARD_SPI_CS_Low();

  /* CMD17: Read single block */
  response = SDCARD_SendCommand(CMD17, block_addr);
  if (response != R1_READY) {
    SDCARD_SPI_CS_High();
    return SDCARD_ERROR_READ;
  }

  /* Wait for data start token */
  timeout = 0xFFFF;
  do {
    response = SDCARD_SPI_TransmitReceive(0xFF);
    timeout--;
  } while ((response != DATA_START_TOKEN) && (timeout > 0));

  if (timeout == 0) {
    SDCARD_SPI_CS_High();
    return SDCARD_ERROR_TIMEOUT;
  }

  /* Read 512 bytes */
  for (int i = 0; i < 512; i++) {
    buffer[i] = SDCARD_SPI_TransmitReceive(0xFF);
  }

  /* Read CRC (2 bytes, ignored) */
  SDCARD_SPI_TransmitReceive(0xFF);
  SDCARD_SPI_TransmitReceive(0xFF);

  /* Deselect card */
  SDCARD_SPI_CS_High();
  SDCARD_SPI_TransmitReceive(0xFF);

  return SDCARD_OK;
}

uint8_t SDCARD_GetType(void) { return card_type; }
