#include <stddef.h>
#include <stdint.h>

void SPI_DMA_Init(void);
void SPI_DMA_FillColor(uint16_t color, uint32_t count);
void SPI_DMA_StartTransfer(void *addr, uint32_t count, uint8_t minc);
