#include "audio_mixer.h"
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
 * @note Enables FPU for hardware floating point operations
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
 * @details HSI (16MHz) -> PLL -> 96MHz SYSCLK
 *          PLLI2S -> 64MHz for I2S audio
 */
void SystemClock_Config(void) {
  /* Enable power interface clock */
  RCC_APB1ENR |= (1 << 28);

  /* Set voltage regulator scale 1 for max performance */
  PWR_CR |= (3 << 14);

  /* Enable HSI and wait for ready */
  RCC_CR |= RCC_CR_HSION;
  while (!(RCC_CR & RCC_CR_HSIRDY))
    ;

  /* Configure Main PLL: HSI/16 * 192 / 2 = 96MHz */
  RCC_PLLCFGR = 16 | (192 << 6) | (0 << 16) | (0 << 22) | (4 << 24);

  /* Configure PLLI2S for 44.1kHz audio: HSI/16 * 271 / 6 ≈ 45.17MHz */
  RCC_PLLI2SCFGR = (271 << 6) | (6 << 28);

  /* Enable both PLLs */
  RCC_CR |= RCC_CR_PLLON | RCC_CR_PLLI2SON;

  /* Wait for PLLs to lock with timeout */
  volatile uint32_t timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLRDY) && timeout++ < 10000)
    ;
  timeout = 0;
  while (!(RCC_CR & RCC_CR_PLLI2SRDY) && timeout++ < 10000)
    ;

  /* Configure Flash: 3 wait states, enable caches and prefetch */
  FLASH_ACR = (1 << 8) | (1 << 9) | (1 << 10) | 3;

  /* Set APB1 prescaler to /2 (48MHz max) */
  RCC_CFGR |= (4 << 10);

  /* Switch to PLL as system clock */
  if (RCC_CR & RCC_CR_PLLRDY) {
    RCC_CFGR &= ~3UL;
    RCC_CFGR |= 2UL;
    while ((RCC_CFGR & (3UL << 2)) != (2UL << 2))
      ;
  }
}

/**
 * @brief Main application entry point
 */
/* Private function prototypes */
static void LoadTestPattern(void);
static void DrawStatusScreen(int sd_status, int file_count, Drumset *drumset);

int main(void) {
  /* Initialize LED on PC13 for status indication */
  RCC_AHB1ENR |= (1 << 2);
  GPIOC_MODER &= ~(3UL << (13 * 2));
  GPIOC_MODER |= (1UL << (13 * 2));
  GPIOC_ODR &= ~(1UL << 13);

  /* Configure system clocks */
  SystemClock_Config();

  /* Initialize PA0 (User Button) as Input with Pull-Up (Active Low) */
  RCC_AHB1ENR |= (1 << 0);          /* GPIOA */
  GPIOA_MODER &= ~(3UL << (0 * 2)); /* Input */
  GPIOA_PUPDR &= ~(3UL << (0 * 2));
  GPIOA_PUPDR |= (1UL << (0 * 2)); /* Pull-Up (01) */

  /* Initialize display */
  SPI_Init();
  ST7789_Init();
  ST7789_Fill(BLACK);

  /* Initialize encoder */
  Encoder_Init();
  Encoder_SetLimits(40, 300); /* BPM range */
  Encoder_SetValue(120);      /* Default BPM */

  /* Initialize sequencer */
  Sequencer_Init();

  /* Initialize audio mixer */
  AudioMixer_Init();

  /* Initialize SD card */
  int sd_status = FAT32_Init();

  /* List files for debugging */
  FAT32_FileEntry files[FAT32_MAX_FILES];
  int file_count = 0;
  if (sd_status == 0) {
    file_count = FAT32_ListRootFiles(files, FAT32_MAX_FILES);
    if (file_count < 0) {
      file_count = -999; // Error indicator
    }
  }

  /* Load drumset (test drumset with silence for now) */
  static Drumset drumset;
  Drumset_Load("/DRUMSETS/KIT001", &drumset);

  /* Set samples for mixer */
  for (int i = 0; i < NUM_CHANNELS; i++) {
    AudioMixer_SetSample(i, drumset.samples[i], drumset.lengths[i]);
  }

  /* Draw status screen */
  DrawStatusScreen(sd_status, file_count, &drumset);

  /* Show first 3 files if available */
  if (sd_status == 0) {
    for (int i = 0; i < file_count && i < 3; i++) {
      ST7789_WriteString(10, 100 + i * 15, files[i].name, YELLOW, BLACK, 1);
    }
  }

  /* Load test pattern */
  LoadTestPattern();

  /* Initialize audio hardware */
  int audio_status = I2S_Init();
  if (audio_status == 0) {
    /* Start DMA with silence */
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
      audio_buffer[i] = 0;
    }
    DMA_Init_I2S(audio_buffer, AUDIO_BUFFER_SIZE);
    I2S_Start();
  }

  /* Boot in STOPPED state */
  ST7789_WriteString(10, 220, "STOPPED ", RED, BLACK, 2);
  uint8_t is_playing = 0;
  uint8_t btn_prev = 1; /* Active Low, default High */
  uint32_t btn_debounce = 0;

  uint32_t last_step = 0xFF;
  int32_t last_encoder = 0;
  int32_t last_increment = 1;

  while (1) {
    /* Poll User Button (PA0) - Active Low (Integrator Debounce) */
    uint8_t btn_curr = (GPIOA_IDR & (1 << 0)) ? 1 : 0;

    /* Count up if pressed (0), Count down if released (1) */
    if (btn_curr == 0) {
      if (btn_debounce < 500)
        btn_debounce++;
    } else {
      if (btn_debounce > 0)
        btn_debounce--;
    }

    /* Trigger on threshold */
    if (btn_debounce >= 400 && !btn_prev) {
      btn_prev = 1; /* Mark as handled */

      /* Button Pressed Action */
      is_playing = !is_playing;
      if (is_playing) {
        Sequencer_Start();
        ST7789_WriteString(10, 220, "PLAYING ", GREEN, BLACK, 2);
      } else {
        Sequencer_Stop();
        GPIOC_ODR |= (1 << 13);
        ST7789_WriteString(10, 220, "STOPPED ", RED, BLACK, 2);
      }
    } else if (btn_debounce == 0) {
      btn_prev = 0; /* Reset state when fully released */
    }

    /* Update encoder button state */
    Encoder_UpdateButton();

    /* Update BPM from encoder */
    int32_t encoder_val = Encoder_GetValue();
    if (encoder_val != last_encoder) {
      Sequencer_SetBPM((uint16_t)encoder_val);
      last_encoder = encoder_val;

      /* Display BPM */
      char buf[32];
      snprintf(buf, sizeof(buf), "BPM: %d   ", (int)encoder_val);
      ST7789_WriteString(10, 140, buf, YELLOW, BLACK, 2);
    }

    /* Check for increment step change */
    int32_t increment = Encoder_GetIncrementStep();
    if (increment != last_increment) {
      last_increment = increment;

      /* Display increment step */
      char buf[32];
      snprintf(buf, sizeof(buf), "Step: x%d  ", (int)increment);
      ST7789_WriteString(10, 200, buf, MAGENTA, BLACK, 1);
    }

    /* Display current step if playing */
    if (is_playing) {
      uint8_t step = Sequencer_GetCurrentStep();
      if (step != last_step) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Step: %02d/%02d  ", step + 1,
                 Sequencer_GetStepCount());
        ST7789_WriteString(10, 170, buf, WHITE, BLACK, 2);

        /* Blink LED on quarter notes (steps 0, 4, 8, 12) */
        if ((step % 4) == 0) {
          GPIOC_ODR &= ~(1 << 13); /* Turn ON (Active Low) */
        } else {
          GPIOC_ODR |= (1 << 13); /* Turn OFF */
        }

        last_step = step;
      }
    }

    /* No WFI here because we need to poll the button eagerly */
    /* Could enable interrupts for PA0 later */
    for (volatile int i = 0; i < 1000; i++)
      ;
  }
}

/**
 * @brief Initialize a test pattern
 */
static void LoadTestPattern(void) {
  /* Kick pattern */
  Sequencer_SetStep(0, 0, 255);  /* Kick on step 0 */
  Sequencer_SetStep(0, 4, 255);  /* Kick on step 4 */
  Sequencer_SetStep(0, 8, 255);  /* Kick on step 8 */
  Sequencer_SetStep(0, 12, 255); /* Kick on step 12 */

  /* Snare pattern */
  Sequencer_SetStep(1, 4, 255);  /* Snare on step 4 */
  Sequencer_SetStep(1, 12, 255); /* Snare on step 12 */

  /* Hats pattern */
  Sequencer_SetStep(2, 2, 200);  /* Hats on step 2 */
  Sequencer_SetStep(2, 6, 200);  /* Hats on step 6 */
  Sequencer_SetStep(2, 10, 200); /* Hats on step 10 */
  Sequencer_SetStep(2, 14, 200); /* Hats on step 14 */

  /* Clap pattern with ghosts, panned left */
  AudioMixer_SetPan(3, 80);      /* Pan Claps Left (Center is 128) */
  Sequencer_SetStep(3, 4, 255);  /* Clap on 4 (Main) */
  Sequencer_SetStep(3, 11, 40);  /* Ghost */
  Sequencer_SetStep(3, 12, 255); /* Clap on 12 (Main) */
  Sequencer_SetStep(3, 14, 60);  /* Ghost */
  Sequencer_SetStep(3, 15, 30);  /* Ghost */
}

/**
 * @brief Draw initial status screen
 */
static void DrawStatusScreen(int sd_status, int file_count, Drumset *drumset) {
  char buf[32];

  ST7789_WriteString(10, 10, "DRUM SEQUENCER", CYAN, BLACK, 2);
  ST7789_WriteString(10, 40, "Phase 2: Audio", WHITE, BLACK, 1);
  ST7789_WriteString(10, 60, sd_status == 0 ? "SD: OK" : "SD: FAIL",
                     sd_status == 0 ? GREEN : RED, BLACK, 1);

  snprintf(buf, sizeof(buf), "Files: %d", file_count);
  ST7789_WriteString(10, 80, buf, WHITE, BLACK, 1);

  /* Show sample loading status */
  ST7789_WriteString(10, 140, "Samples:", WHITE, BLACK, 1);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    snprintf(buf, sizeof(buf), "%s: %lu", drumset->sample_names[i],
             (unsigned long)drumset->lengths[i]);
    ST7789_WriteString(10, 160 + i * 15, buf,
                       drumset->lengths[i] > 1000 ? GREEN : RED, BLACK, 1);
  }
}
