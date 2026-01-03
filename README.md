# STM32F411 Eurorack Drum Sequencer

A 6-channel, 32-step drum sequencer for Eurorack modular synthesizers, built on the STM32F411CEU6 "Black Pill" board.

## Features

### Audio Engine 🎧
- **6-Channel Mixing**: Kick, Snare, Hats, Clap, Perc1, Perc2.
- **WAV Playback**: Loads samples from SD Card (FAT32).
- **High Fidelity**: 44.1kHz stereo output via I2S (PCM5102A).
- **Dynamic Mixing**: Per-channel volume and panning.
- **Kit System**: Save and Load up to 100 full drum kits (KIT-001 to KIT-100).
- **Auto-Load**: Automatically loads `KIT-001` on startup for instant playability.

### Sequencer 🎹
- **32 Steps**: Expanded 2-bar step sequencing.
- **24 PPQN Clock**: High-resolution internal clock.
- **Hardware Sync Output**: 24 PPQN 50% duty cycle clock on **PA15**.
- **Adjustable BPM**: 40-300 BPM via rotary encoder.

### User Interface 🖥️
- **Display**: ST7789 320×240 IPS LCD.
- **Real-time Feedback**: 
  - Dynamic Step Counter (e.g., "01/32").
  - Active Step Visualizers.
  - Large Status Indicator (PLAYING/STOPPED).
  - **Kit Info**: Currently loaded kit name displayed in footer.
- **Safety Guards**: Playback-locked menus to prevent accidental changes during performance.

## Hardware

### Microcontroller
- **STM32F411CEU6** "Black Pill" (100MHz Cortex-M4F, 128KB RAM).

### Pinout
| Pin | Function | Notes |
|-----|----------|-------|
| **PA15** | **Clock Out** | **TIM2_CH1 PWM (24 PPQN)** |
| PA1-PA4 | Display | CS, DC, RES, BLK |
| PA5, PA7 | SPI1 | Display SCK, MOSI |
| PB0 | SD CS | Chip Select |
| PB3-PB5 | SPI3 | SD Card (SCK, MISO, MOSI) |
| PB6-PB8 | Encoder | A, B, Button |
| PB10,12,15| I2S2 | Audio DAC (LCK, BCK, DIN) |
| PC13 | LED | Status Heartbeat |

## Getting Started

### Prerequisites
- `arm-none-eabi-gcc` toolchain
- `dfu-util` for flashing
- `make`

### Building
```bash
make clean && make
```

### Flashing
1. Put the device in DFU mode (Hold BOOT0, press NRST).
2. Run:
```bash
make flash
```

## SD Card Structure
Format SD card as **FAT32**.

```
/
├── DRUMSETS/
│   ├── KIT-001.DRM   (Text-based kit definition)
│   └── ...
└── SAMPLES/
    ├── KICK.WAV
    ├── SNARE.WAV
    ├── HATS.WAV
    └── ...
```
*Note: Samples must be 44.1kHz, 16-bit PCM Mono.*

## Development Status
✅ **Phase 5 Complete**:
- [x] **Full Save/Load System**: 100 Slots (KIT-001.DRM).
- [x] **Robust Boot Sequence**: Auto-load mixer sync fixed.
- [x] **UI Polish**: "Kit" terminology, flicker-free rendering, playback guards.
- [x] **Code Cleanup**: Removed legacy test patterns and dead code.
- [x] **Performance**: Optimized directory scanning and display updates.

## Credits

### Design & Direction
**Arda Eden** - Main Designer, Project Lead.

### Implementation
**Google Gemini 2.0 Flash (Thinking - Experimental)** - AI Coder.
