# STM32F411 Eurorack Drum Sequencer

A 4-channel, 32-step drum sequencer for Eurorack modular synthesizers, built on the STM32F411CEU6 "Black Pill" board.

## Features

### Phase 2 Complete ✅
- **4-Channel Drum Sequencer**
  - 16 steps per track
  - Velocity control per step
  - Ghost notes and dynamics
  
- **High-Quality Audio**
  - 44.1kHz stereo output via I2S
  - PCM5102A DAC integration
  - 4-channel mixer with panning
  - Glitch-free playback
  
- **WAV Sample Playback**
  - Load samples from SD card (FAT32)
  - Multi-sector file reading
  - Supports: KICK.WAV, SNARE.WAV, HATS.WAV, CLAP.WAV
  
- **Precise Timing**
  - 24 PPQN clock system
  - BPM control via rotary encoder (40-300 BPM)
  - Smooth BPM changes without glitches

- **Display**
  - ST7789 320×240 color LCD
  - Real-time status and file listing

### Future Enhancements
- Interactive grid UI for pattern editing
- Pattern save/load functionality
- Additional hardware controls

## Hardware

### MCU
- **STM32F411CEU6** "Black Pill"
  - 100MHz ARM Cortex-M4F
  - 128KB RAM
  - 512KB Flash

### Peripherals
- **Display**: ST7789V (SPI1)
- **Audio DAC**: PCM5102A (I2S2) - *planned*
- **Storage**: MicroSD Card (SPI3)
- **Input**: Rotary Encoder (PB6, PB7, PB8)

### Pin Assignment
| Pin | Function | Notes |
|-----|----------|-------|
| PA1-PA4 | Display Control | CS, DC, RES, BLK |
| PA5, PA7 | SPI1 | Display SCK, MOSI |
| PB0 | SD Card CS | |
| PB3-PB5 | SPI3 | SD Card (requires JTAG disable) |
| PB6-PB8 | Encoder | A, B, Button |
| PB10, PB12, PB15 | I2S2 | Audio (planned) |
| PC13 | LED | Status indicator |

## Building

### Prerequisites
```bash
arm-none-eabi-gcc
make
dfu-util
```

### Compile
```bash
make
```

### Flash
```bash
make flash
```

## Project Structure

```
DrumModule/
├── main.c              # Main application
├── encoder.c/h         # Rotary encoder driver
├── sequencer_clock.c/h # 24 PPQN clock system
├── sequencer.c/h       # Sequencer engine
├── st7789.c/h          # Display driver
├── spi.c/h             # SPI1 driver
├── sdcard_spi.c/h      # SPI3 driver for SD
├── sdcard.c/h          # SD card protocol
├── fat32.c/h           # FAT32 filesystem
├── i2s.c/h             # I2S audio (planned)
├── dma.c/h             # DMA for audio (planned)
└── Makefile            # Build configuration
```

## Memory Usage

```
RAM:   ~110KB used (100KB for WAV samples)
Flash: ~14.6KB code
Samples: 4 channels × 25KB each = 100KB
```

## Development

### Branch Structure
- `main` - Stable releases
- `feature/sequencer` - Active development

### Current Status
✅ Phase 2 Complete - Production Ready
� Binary Size: 14660 bytes
🎵 Audio: Perfect 44.1kHz playback

## AI Contributions

This project was developed with assistance from **Google Gemini 2.0 Flash (Thinking - Experimental)** AI assistant. Key contributions include:

### Architecture & Planning
- System architecture design for 4-channel sequencer
- Memory budget analysis and optimization
- Pin assignment strategy
- 24 PPQN clock timing calculations
- Multi-phase implementation roadmap

### Core Implementation
- **Rotary Encoder Driver** (`encoder.c/h`)
  - EXTI interrupt-based quadrature decoding
  - Both-edge detection for accurate step counting
  - Debounced button handling with x1/x10 toggle
  - Fixed polarity and sensitivity issues

- **Clock System** (`sequencer_clock.c/h`)
  - TIM2-based 24 PPQN implementation
  - Safe BPM changes without stopping sequencer
  - Precise timing with 1MHz timer base
  - Callback system for sequencer integration

- **Sequencer Engine** (`sequencer.c/h`)
  - Pattern storage (4 channels × 32 steps)
  - Step counter with 16th note resolution
  - Integration with clock callbacks
  - Pattern editing API

### Problem Solving
- Fixed encoder counting by 2 (edge detection issue)
- Corrected encoder polarity
- Resolved sequencer stopping during rapid BPM changes
- Optimized timer reload for stable operation

### Code Quality
- Comprehensive documentation and comments
- Clean code structure and organization
- Proper error handling
- Memory-efficient implementation

### Documentation
- Implementation plans and walkthroughs
- Pin assignment documentation
- Build instructions
- This README

## Credits

### Design & Direction
**Arda Eden** - Main Designer, Project Lead, and Prompter
- System architecture and feature design
- Hardware selection and integration
- Project vision and requirements
- Testing and validation

### Implementation
**Google Gemini 2.0 Flash (Thinking - Experimental)** - AI Coder
- Complete codebase implementation
- Driver development (I2S, DMA, SPI, Encoder, Display)
- Audio engine and sequencer logic
- FAT32 filesystem and WAV loading
- Debugging and optimization
- Documentation

## License

*[Add your license here]*

---

**Status**: Phase 2 Complete ✅ - Production Ready  
**Binary Size**: 14660 bytes  
**Latest Commit**: 4c7e53d (Code cleanup)
