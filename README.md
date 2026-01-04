# STM32F411 Eurorack Drum Sequencer

A 6-channel, 32-step drum sequencer for Eurorack modular synthesizers, built on the STM32F411CEU6 "Black Pill" board.

## Features

### Audio Engine 🎧
- **6-Channel Mixing**: For using as Kick, Snare, Hats, Clap, Perc1, Perc2. etc.
- **WAV Playback**: Loads samples from SD Card (FAT32).
- **High Fidelity**: 44.1kHz stereo output via I2S (PCM5102A).
- **Dynamic Mixing**: Per-channel volume and panning.
- **Kit System**: Save and Load up to 100 full drum kits (KIT-001 to KIT-100).
- **Auto-Load**: Automatically loads `KIT-001` and `PAT-001` on startup for instant playability.

### Sequencer 🎹
- **32 Steps**: Expanded 4-bar step sequencing.
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
├── PATTERNS/
│   ├── PAT-001.PAT   (Binary pattern data)
│   └── ...
└── SAMPLES/
    ├── KICK.WAV
    ├── SNARE.WAV
    └── ...
```
*Note: Samples must be 44.1kHz, 16-bit PCM Mono.*

## File Formats

### Drumset (.DRM)
The drumset files are **text-based** (ASCII) and stored in the `/DRUMSETS/` directory. Each file contains exactly 6 lines, corresponding to the 6 internal channels.

**Row Format:**
`channel_index,sample_path,volume,pan\n`

| Field | Type | Range / Description |
|-------|------|---------------------|
| **channel_index** | Integer | `0` to `5` (Matches Hardware Channel) |
| **sample_path** | String | Max 64 chars. Relative to SD root (e.g., `SAMPLES/KICK.WAV`) |
| **volume** | Integer | `0` to `255` (0 = Mute, 255 = Max) |
| **pan** | Integer | `0` to `255` (0 = Left, 128 = Center, 255 = Right) |

*Example Line:* `0,SAMPLES/KICK.WAV,250,128`

---

### Pattern (.PAT)
The pattern files are **binary-based** memory dumps of the `Pattern` C-struct (~211-212 bytes), stored in the `/PATTERNS/` directory.

**Structure Layout (Little Endian):**

| Offset | Size | Name | Description |
|--------|------|------|-------------|
| **0**  | 192 | `steps` | 2D Array [6][32]. Bytes store Velocity (0-255). |
| **192**| 1   | `step_count` | Number of active steps in the loop (1-32). |
| **193**| (1) | *padding*| Compiler alignment padding (internal use). |
| **194**| 2   | `bpm` | Uint16 value representing current tempo. |
| **196**| 16  | `name` | Char array containing the pattern name. |

*Note: Total file size is exactly 212 bytes due to struct alignment.*

## Development Status
✅ **Phase 6 Complete**:
- [x] **Pattern Management**: Save/Load up to 100 patterns (PAT-001.PAT).
- [x] **Sync Output**: 24 PPQN clock output on PA15 for external gear.
- [x] **UI Refinement**: Fixed Pattern Menu Save/Load glitch and background redraw overlaps.
- [x] **Stability**: Improved directory scanning and error handling for missing files.

## Credits

### Design & Direction
**Arda Eden** - Main Designer, Project Lead.

### Implementation
**Antigravity** - AI Coding Assistant (Google Deepmind).
