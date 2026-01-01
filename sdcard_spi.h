#ifndef SDCARD_SPI_H
#define SDCARD_SPI_H

#include <stdint.h>

/**
 * @brief Initialize SPI3 for SD card communication
 * @details Configures PB3 (SCK), PB4 (MISO), PB5 (MOSI) as AF6
 *          PB0 as CS (GPIO output)
 *          Disables JTAG to free up PB3/PB4
 */
void SDCARD_SPI_Init(void);

/**
 * @brief Set SD card CS pin low (select)
 */
void SDCARD_SPI_CS_Low(void);

/**
 * @brief Set SD card CS pin high (deselect)
 */
void SDCARD_SPI_CS_High(void);

/**
 * @brief Transmit and receive one byte via SPI3
 * @param data Byte to transmit
 * @return Received byte
 */
uint8_t SDCARD_SPI_TransmitReceive(uint8_t data);

/**
 * @brief Set SPI3 to slow speed (~400kHz for initialization)
 */
void SDCARD_SPI_SetSlowSpeed(void);

/**
 * @brief Set SPI3 to fast speed (~12MHz for data transfer)
 */
void SDCARD_SPI_SetFastSpeed(void);

#endif
