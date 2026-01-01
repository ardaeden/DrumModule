# STM32F411 Eurorack Drum Sequencer

A 4-channel, 32-step drum sequencer for Eurorack modular synthesizers, built on the STM32F411CEU6 "Black Pill" board.

## Features

### Current (Phase 1 - Core Infrastructure) ✅
- **Rotary Encoder Control**
  - BPM adjustment (40-300 BPM)
  - Increment toggle (x1 / x10) via button press
  - EXTI interrupt-driven with debouncing
  
- **Precise Clock System**
  - 24 PPQN (Pulses Per Quarter Note) standard
  - Hardware timer (TIM2) for jitter-free timing
  - Stable operation during BPM changes
  
- **Sequencer Engine**
  - 4 channels (Kick, Snare, Hat, Perc)
  - 32 steps (adjustable)
  - 16th note resolution
  - Pattern storage with velocity

- **Display**
  - ST7789 320×240 color LCD
  - Real-time BPM and step display
  - Status indicators

### Planned (Future Phases)
- **Phase 2**: Audio Engine (WAV playback, 4-channel mixer)
- **Phase 3**: Pattern editing and sequencer logic
- **Phase 4**: Full UI with grid view
- **Phase 5**: SD card drumset/pattern management

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
RAM:  ~6KB used, ~122KB free for samples
Flash: ~14KB code
```

## Development

### Branch Structure
- `main` - Stable releases
- `feature/sequencer` - Active development

### Current Status
✅ Phase 1 Complete (Core Infrastructure)
🚧 Phase 2 In Progress (Audio Engine)

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

## License

*[Add your license here]*

## Author

Arda Eden

**AI Assistant**: Google Gemini 2.0 Flash (Thinking - Experimental)
- Architecture design
- Core driver implementation
- Debugging and optimization
- Documentation

---

**Status**: Phase 1 Complete ✅  
**Next**: Phase 2 - Audio Engine (WAV playback & mixer)
