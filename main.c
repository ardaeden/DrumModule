#include "audio_mixer.h"
#include "buttons.h"
#include "dma.h"
#include "encoder.h"
#include "fat32.h"
#include "i2s.h"
#include "sequencer.h"
#include "spi.h"
#include "st7789.h"
#include "wav_loader.h"
#include <stdint.h>
#include <stdio.h>

/* STM32F411 Register Definitions */
#define PERIPH_BASE 0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define RCC_BASE (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOC_BASE (AHB1PERIPH_BASE + 0x0800UL)
#define PWR_BASE (PERIPH_BASE + 0x00007000UL)
#define FLASH_BASE (AHB1PERIPH_BASE + 0x3C00UL)

/* RCC Registers */
#define RCC_CR (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR (*(volatile uint32_t *)(RCC_BASE + 0x08))
#define RCC_APB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_AHB1ENR (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_PLLI2SCFGR (*(volatile uint32_t *)(RCC_BASE + 0x84))

/* Power and Flash Registers */
#define PWR_CR (*(volatile uint32_t *)(PWR_BASE + 0x00))
#define FLASH_ACR (*(volatile uint32_t *)(FLASH_BASE + 0x00))

/* GPIO Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x00))
#define GPIOA_PUPDR (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x0C))
#define GPIOA_IDR (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x10))

#define GPIOC_MODER (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_ODR (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

/* RCC Control Register Bits */
#define RCC_CR_HSION (1 << 0)
#define RCC_CR_HSIRDY (1 << 1)
#define RCC_CR_PLLON (1 << 24)
#define RCC_CR_PLLRDY (1 << 25)
#define RCC_CR_PLLI2SON (1 << 26)
#define RCC_CR_PLLI2SRDY (1 << 27)

/**
 * @brief System initialization required by startup code
 */
void SystemInit(void) {
  /* Enable FPU: Set CP10 and CP11 to full access */
  (*(volatile uint32_t *)0xE000ED88) |= (0xF << 20);
}

/**
 * @brief Dummy init function to satisfy libc
 */
void _init(void) {}

/**
 * @brief Configure system clocks
 */
void SystemClock_Config(void) {
  RCC_APB1ENR |= (1 << 28);
  PWR_CR |= (3 << 14);
  RCC_CR |= RCC_CR_HSION;
  while (!(RCC_CR & RCC_CR_HSIRDY))
    ;

  RCC_PLLCFGR = 16 | (192 << 6) | (0 << 16) | (0 << 22) | (4 << 24);
  RCC_PLLI2SCFGR = (271 << 6) | (6 << 28);
  RCC_CR |= RCC_CR_PLLON | RCC_CR_PLLI2SON;

  volatile uint32_t timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLRDY) && timeout++ < 10000)
    ;
  timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLI2SRDY) && timeout++ < 10000)
    ;

  FLASH_ACR = (1 << 8) | (1 << 9) | (1 << 10) | 3;
  RCC_CFGR |= (4 << 10);

  if (RCC_CR & RCC_CR_PLLRDY) {
    RCC_CFGR &= ~3UL;
    RCC_CFGR |= 2UL;
    while ((RCC_CFGR & (3UL << 2)) != (2UL << 2))
      ;
  }
}

/* Private function prototypes */
static void LoadTestPattern(void);
static void DrawMainScreen(Drumset *drumset);
static void UpdateBlinker(uint8_t channel, uint8_t active);
static void OnButtonEvent(uint8_t button_id, uint8_t pressed);

/* Global state for display and control */
static volatile uint8_t is_playing = 0;
static uint32_t last_step = 0xFF;
static uint8_t channel_states[NUM_CHANNELS] = {0};
static volatile uint8_t needs_ui_refresh = 0;

/**
 * @brief Main application entry point
 */
int main(void) {
  /* Initialize LED */
  RCC_AHB1ENR |= (1 << 2);
  GPIOC_MODER &= ~(3UL << (13 * 2));
  GPIOC_MODER |= (1UL << (13 * 2));
  GPIOC_ODR |= (1UL << 13); /* OFF (Active Low) */

  SystemClock_Config();
  SPI_Init();
  ST7789_Init();
  ST7789_Fill(BLACK);

  Encoder_Init();
  Encoder_SetLimits(40, 300);
  Encoder_SetValue(120);

  Sequencer_Init();
  Button_Init();
  Button_SetCallback(OnButtonEvent);

  AudioMixer_Init();

  /* SD initialization and sample loading */
  (void)FAT32_Init();
  static Drumset drumset;
  Drumset_Load("/DRUMSETS/KIT001", &drumset);

  for (int i = 0; i < NUM_CHANNELS; i++) {
    AudioMixer_SetSample(i, drumset.samples[i], drumset.lengths[i]);
  }

  /* Start audio subsystem as early as possible for PLLI2S stability */
  int audio_status = I2S_Init();
  if (audio_status == 0) {
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
      audio_buffer[i] = 0;
    DMA_Init_I2S(audio_buffer, AUDIO_BUFFER_SIZE);
    I2S_Start();
  }

  DrawMainScreen(&drumset);
  LoadTestPattern();

  int32_t last_encoder = 0;
  int32_t last_increment = 0;

  while (1) {
    /* Handle UI refresh when playback stops */
    if (needs_ui_refresh) {
      needs_ui_refresh = 0;
      last_step = 0xFF;

      /* Reset STEP counter display */
      ST7789_WriteString(240, 10, "01/16      ", WHITE, BLACK, 2);

      /* Reset any active blinkers without full screen redraw */
      for (int i = 0; i < NUM_CHANNELS; i++) {
        if (channel_states[i]) {
          UpdateBlinker(i, 0);
          channel_states[i] = 0;
        }
      }
      GPIOC_ODR |= (1 << 13); /* LED OFF */
    }

    /* Update status text when play state changes */
    static uint8_t last_playing = 0xFF;
    if (is_playing != last_playing) {
      const char *status = is_playing ? "PLAYING" : "STOPPED";
      uint16_t status_color = is_playing ? GREEN : RED;
      ST7789_WriteString(10, 220, status, status_color, BLACK, 2);
      last_playing = is_playing;
    }

    /* Handle BPM updates from encoder */
    int32_t encoder_val = Encoder_GetValue();
    if (encoder_val != last_encoder) {
      Sequencer_SetBPM((uint16_t)encoder_val);
      last_encoder = encoder_val;

      char val_buf[16];
      snprintf(val_buf, sizeof(val_buf), "%d ", (int)encoder_val);
      uint16_t val_color = (Encoder_GetIncrementStep() == 10) ? MAGENTA : CYAN;
      ST7789_WriteString(10, 10, "BPM:", CYAN, BLACK, 2);
      ST7789_WriteString(60, 10, val_buf, val_color, BLACK, 2);
    }

    /* Handle encoder increment step changes */
    int32_t increment = Encoder_GetIncrementStep();
    if (increment != last_increment) {
      last_increment = increment;
      char val_buf[16];
      snprintf(val_buf, sizeof(val_buf), "%d ", (int)Encoder_GetValue());
      uint16_t val_color = (increment == 10) ? MAGENTA : CYAN;
      ST7789_WriteString(10, 10, "BPM:", CYAN, BLACK, 2);
      ST7789_WriteString(60, 10, val_buf, val_color, BLACK, 2);
    }

    if (is_playing) {
      uint8_t step = Sequencer_GetCurrentStep();
      if (step != last_step) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d/%02d      ", step + 1,
                 Sequencer_GetStepCount());
        ST7789_WriteString(240, 10, buf, WHITE, BLACK, 2);

        /* Update blinkers based on pattern triggers */
        for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
          uint8_t velocity = Sequencer_GetStep(i, step);
          uint8_t active = (velocity > 0);
          if (active != channel_states[i]) {
            UpdateBlinker(i, active);
            channel_states[i] = active;
          }
        }

        /* LED Blink on quarter notes */
        if ((step % 4) == 0) {
          GPIOC_ODR &= ~(1 << 13); /* ON */
        } else {
          GPIOC_ODR |= (1 << 13); /* OFF */
        }
        last_step = step;
      }
    }

    __asm volatile("wfi");
  }
}

static void LoadTestPattern(void) {
  /* Clear pattern */
  Sequencer_ClearPattern();

  /* KICK (Ch 0): Minimal House (4-on-the-floor)
   * 1 . . . 2 . . . 3 . . . 4 . . .
   */
  Sequencer_SetStep(0, 0, 200);
  Sequencer_SetStep(0, 4, 200);
  Sequencer_SetStep(0, 8, 200);
  Sequencer_SetStep(0, 12, 200);

  /* SNARE (Ch 1): Classic Backbeat
   * . . . . X . . . . . . . X . . .
   */
  Sequencer_SetStep(1, 4, 255);  // Backbeat on 2
  Sequencer_SetStep(1, 12, 255); // Backbeat on 4

  /* HATS (Ch 2): Pure Off-beat Open Hat
   * . . X . . . X . . . X . . . X .
   */
  Sequencer_SetStep(2, 2, 180);
  Sequencer_SetStep(2, 6, 180);
  Sequencer_SetStep(2, 10, 180);
  Sequencer_SetStep(2, 14, 180);

  /* CLAP (Ch 3): Deep Dub Echo feel
   * One hit with delay simulation
   */
  AudioMixer_SetPan(3, 100);    // Left-ish
  Sequencer_SetStep(3, 4, 200); // The Hit
  Sequencer_SetStep(3, 7, 60);  // Echo 1
  Sequencer_SetStep(3, 10, 30); // Echo 2

  /* PERC 1 (Ch 4): Minimal Glitch
   * Random-ish sparse hits
   */
  AudioMixer_SetPan(4, 220); // Right
  Sequencer_SetStep(4, 11, 150);
  Sequencer_SetStep(4, 15, 100);

  /* PERC 2 (Ch 5): Sub assitance / Texture
   * Just one texture hit
   */
  AudioMixer_SetPan(5, 128);
  Sequencer_SetStep(5, 0, 80);
}

static void DrawMainScreen(Drumset *drumset) {
  ST7789_Fill(BLACK);

  ST7789_WriteString(10, 10, "BPM:", CYAN, BLACK, 2);
  char val_buf[16];
  snprintf(val_buf, sizeof(val_buf), "%d", (int)Encoder_GetValue());
  ST7789_WriteString(60, 10, val_buf, CYAN, BLACK, 2);
  ST7789_WriteString(240, 10, "01/16", WHITE, BLACK, 2);

  /* Status indicator */
  const char *status = is_playing ? "PLAYING" : "STOPPED";
  uint16_t status_color = is_playing ? GREEN : RED;
  ST7789_WriteString(10, 220, status, status_color, BLACK, 2);

  /* 3x2 Grid Layout
   * Width 90px, Height 80px
   * Row 1 Y=40, Row 2 Y=130
   * Col 1 X=10, Col 2 X=110, Col 3 X=210
   */

  /* Channel 0: Red */
  ST7789_FillRect(10, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(10, 40, 90, 80, 2, RED);
  ST7789_WriteString(15, 50, drumset->sample_names[0], RED, BLACK, 1);

  /* Channel 1: Green */
  ST7789_FillRect(110, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(110, 40, 90, 80, 2, GREEN);
  ST7789_WriteString(115, 50, drumset->sample_names[1], GREEN, BLACK, 1);

  /* Channel 2: Yellow */
  ST7789_FillRect(210, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(210, 40, 90, 80, 2, YELLOW);
  ST7789_WriteString(215, 50, drumset->sample_names[2], YELLOW, BLACK, 1);

  /* Channel 3: Magenta */
  ST7789_FillRect(10, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(10, 130, 90, 80, 2, MAGENTA);
  ST7789_WriteString(15, 140, drumset->sample_names[3], MAGENTA, BLACK, 1);

  /* Channel 4: Cyan */
  ST7789_FillRect(110, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(110, 130, 90, 80, 2, CYAN);
  ST7789_WriteString(115, 140, drumset->sample_names[4], CYAN, BLACK, 1);

  /* Channel 5: Orange */
  ST7789_FillRect(210, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(210, 130, 90, 80, 2, ORANGE);
  ST7789_WriteString(215, 140, drumset->sample_names[5], ORANGE, BLACK, 1);
}

static void UpdateBlinker(uint8_t channel, uint8_t active) {
  uint16_t x, y, base_color;

  switch (channel) {
  case 0: /* Red */
    x = 10;
    y = 40;
    base_color = RED;
    break;
  case 1: /* Green */
    x = 110;
    y = 40;
    base_color = GREEN;
    break;
  case 2: /* Yellow */
    x = 210;
    y = 40;
    base_color = YELLOW;
    break;
  case 3: /* Magenta */
    x = 10;
    y = 130;
    base_color = MAGENTA;
    break;
  case 4: /* Cyan */
    x = 110;
    y = 130;
    base_color = CYAN;
    break;
  case 5: /* Orange */
    x = 210;
    y = 130;
    base_color = ORANGE;
    break;
  default:
    return;
  }

  uint16_t frame_color = active ? WHITE : base_color;
  uint16_t thickness = active ? 4 : 2; // Reduced thickness for smaller boxes

  ST7789_DrawThickFrame(x, y, 90, 80, thickness, frame_color);

  // Clear inner frame when deactivated to remove thickness artifact
  if (!active) {
    ST7789_DrawThickFrame(x + 2, y + 2, 86, 76, 2, BLACK);
  }
}

static void OnButtonEvent(uint8_t button_id, uint8_t pressed) {
  if (pressed) {
    if (button_id == BUTTON_START) {
      is_playing = !is_playing;
      if (is_playing) {
        Sequencer_Start();
        GPIOC_ODR &= ~(1 << 13); /* ON */
      } else {
        Sequencer_Stop();
        GPIOC_ODR |= (1 << 13); /* OFF */
        needs_ui_refresh = 1;
      }
    } else if (button_id == BUTTON_ENCODER) {
      Encoder_ToggleIncrement();
    }
  }
}
