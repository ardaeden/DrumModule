# STM32F411CEU6 Drum Machine Pinout

## Overview
This project uses the STM32F411CEU6 "Black Pill" board.
- **Display**: ST7789V (SPI)
- **Audio**: PCM5102A (I2S)
- **Storage**: MH-SD Card Module (SPI)

## Pin Mapping

### ST7789V Display (SPI1)
| Interface Pin | STM32 Pin | Function |
|:---:|:---:|---|
| **CS** | **PA4** | SPI1_NSS (Chip Select) |
| **SCK** | **PA5** | SPI1_SCK (Clock) |
| **MOSI** | **PA7** | SPI1_MOSI (Data) |
| **DC** | **PA2** | Data/Command Control |
| **RES** | **PA3** | Reset |
| **BLK** | **PA1** | Backlight Control (PWM/GPIO) |

*Note: SPI1_MISO (PA6) is not used for this display.*

### PCM5102A Audio DAC (I2S2)
| Interface Pin | STM32 Pin | Function |
|:---:|:---:|---|
| **BCK** | **PB10** | I2S2_CK (Bit Clock) |
| **LRCK** | **PB12** | I2S2_WS (Word Select) |
| **DIN** | **PB15** | I2S2_SD (Data) |
| **SCK/MCLK**| **-** | Not Connected (Configured for 3-wire I2S) |

| **SCK/MCLK**| **-** | Not Connected (Configured for 3-wire I2S) |

*Note: You MUST configure your PCM5102A module to generate its own system clock (Internal PLL).*

### MH-SD Card Module (SPI1 - Shared)
| Interface Pin | STM32 Pin | Function | Notes |
|:---:|:---:|---|---|
| **CS** | **PB0** | Chip Select | |
| **SCK** | **PA5** | SPI1_SCK | Shared with Display |
| **MOSI** | **PA7** | SPI1_MOSI | Shared with Display |
| **MISO** | **PA6** | SPI1_MISO | |
| **VCC** | **5V** | Power | |
| **GND** | **GND** | Ground | |

### Power Connections
- **VIN / VCC**: **5V** (Preferred for modules with onboard regulator)
- **GND**: **GND**
- **3.3V Pad**: Leave unconnected (unless bypassing regulator)

### Other
- **User LED**: PC13 (Active Low)
- **User Button**: PA0
- **Debug**: SWDIO (PA13), SWCLK (PA14)
