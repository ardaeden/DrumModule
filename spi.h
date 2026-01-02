#ifndef SPI_H
#define SPI_H

#include <stdint.h>

void SPI_Init(void);
void SPI_Transmit(uint8_t data);
void SPI_WriteData8(uint8_t data);
void SPI_WriteData16(uint16_t data);
void SPI_SetDataSize16(void);
void SPI_SetDataSize8(void);
void SPI_WaitBusy(void);

#endif
