#include "audio_mixer.h"
#include "buttons.h"
#include "dma.h"
#include "encoder.h"
#include "fat32.h"
#include "i2s.h"
#include "pattern_manager.h"
#include "sequencer.h"
#include "spi.h"
#include "st7789.h"
#include "wav_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Color definitions */
#ifndef GRAY
#define GRAY 0x7BEF
#endif
#ifndef DARKBLUE
#define DARKBLUE 0x0010
#endif

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

/* SysTick Registers */
#define STK_CTRL (*(volatile uint32_t *)0xE000E010)
#define STK_LOAD (*(volatile uint32_t *)0xE000E014)
#define STK_VAL (*(volatile uint32_t *)0xE000E018)
#define STK_CALIB (*(volatile uint32_t *)0xE000E01C)
#define NVIC_IPR_BASE ((volatile uint8_t *)0xE000E400)

/* GPIO Registers */
#define GPIOA_MODER (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x00))
#define GPIOA_PUPDR (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x0C))
#define GPIOA_IDR (*(volatile uint32_t *)(AHB1PERIPH_BASE + 0x0000UL + 0x10))

#define GPIOC_MODER (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_ODR (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

#define GPIOB_BASE (AHB1PERIPH_BASE + 0x0400UL)
#define GPIOB_IDR (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

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

static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void) { ms_ticks++; }

uint32_t HAL_GetTick(void) { return ms_ticks; }

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
static void DrawDrumsetMenu(uint8_t full_redraw);
static void DrawPatternMenu(uint8_t full_redraw);
static void ExitPatternMenu(void);
static void UpdateModeUI(void);
static void UpdateBlinker(uint8_t channel, uint8_t active);
static void OnButtonEvent(uint8_t button_id, uint8_t pressed);
static void DrawStepEditScreen(uint8_t full_redraw);
static void ShowPopup(const char *msg, uint16_t color, uint8_t exit_type);

/* Global state for display and control */
static volatile uint8_t is_playing = 0;
static volatile uint8_t is_edit_mode = 0; /* 0=Normal, 1=Drumset Edit */
/* Channel Edit Mode States: 0=Off, 1=Menu, 2=Browser, 3=Vol, 4=Pan */
static volatile uint8_t is_channel_edit_mode = 0;
static volatile uint8_t selected_channel = 0;
static volatile uint32_t saved_bpm = 120;
static volatile uint8_t mode_changed = 0;
static volatile uint8_t is_pattern_edit_mode = 0;
static volatile uint8_t is_pattern_detail_mode = 0;
static volatile int8_t pattern_cursor = 0; /* Step cursor for detail mode */

static uint32_t last_step = 0xFF;
static uint8_t channel_states[NUM_CHANNELS] = {0};
static volatile uint8_t full_redraw_needed = 0;
static volatile uint8_t needs_ui_refresh = 0;
static volatile uint8_t needs_step_update = 0;
static volatile uint8_t needs_full_grid_update =
    0;                                   /* For flicker-free pattern swap */
static volatile uint8_t is_ui_popup = 0; /* Success or Error */
static uint32_t ui_popup_start_time = 0;
static uint8_t ui_popup_exit_type = 0; /* 0=None, 1=Drumset, 2=Pattern */

/* Incremental UI tracking states */
static int last_bpm = -1;
static int last_is_edit = -1;
static int last_is_pattern_edit = -1;
static int last_is_playing = -1;
static uint8_t last_drawn_channel = 0xFF;

static FAT32_FileEntry file_list[FAT32_MAX_FILES];
static int file_count = 0;
static int selected_file_index = 0;
static int last_selected_file_index = 0;
static int edit_menu_index = 0; /* 0=Sample, 1=Vol, 2=Pan */
static int last_menu_index = 0;
static Drumset *current_drumset = NULL;
static uint32_t current_cluster = 0; /* Current directory cluster for browser */
static char browser_path[128] = "SAMPLES";

/* Drumset Menu States: 0=Off, 1=Menu, 2=Save Slots, 3=Load Slots */
static volatile uint8_t is_drumset_menu_mode = 0;
static int drumset_menu_index = 0;  /* 0=Save, 1=Load, 2=Back */
static uint8_t selected_slot = 1;   /* Current slot selection (1-100) */
static uint8_t occupied_slots[100]; /* List of occupied slots */
static int occupied_slot_count = 0;
static uint8_t loaded_pattern_slot = 0; /* 0 means none or default */

/* Long-press detection */
static uint32_t button_drumset_start_time = 0;
static uint8_t button_drumset_handled = 0;
static uint8_t button_drumset_pressed = 0;

static uint32_t button_pattern_start_time = 0;
static uint8_t button_pattern_handled = 0;
static uint8_t button_pattern_pressed = 0;

/* Pattern Menu States: 0=Off, 1=Menu, 2=Save Slots, 3=Load Slots */
static volatile uint8_t is_pattern_menu_mode = 0;
static int pattern_menu_index = 0; /* 0=Save, 1=Load, 2=Back */

/* Cyan Color for Pattern Menu */
#define CYAN 0x07FF

/* Helper: case-insensitive string ends with check */
static int str_ends_with(const char *str, const char *suffix) {
  int str_len = strlen(str);
  int suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return 0;
  return strcasecmp(str + str_len - suffix_len, suffix) == 0;
}

static void ScanDirectory(void) {
  /* Use static buffer to avoid stack overflow (~1KB) */
  static FAT32_FileEntry all_files[FAT32_MAX_FILES];
  int count = FAT32_ListDir(current_cluster, all_files, FAT32_MAX_FILES);

  file_count = 0;

  /* Add [EMPTY] option at root level */
  if (current_cluster == FAT32_GetRootCluster()) {
    strcpy(file_list[file_count].name, "[EMPTY]");
    file_list[file_count].is_dir = 0;
    file_list[file_count].size = 0;
    file_list[file_count].first_cluster = 0;
    file_count++;
  }

  /* Filter pass */
  for (int i = 0; i < count; i++) {
    /* Skip dotfiles (hidden), but allow ".." */
    if (all_files[i].name[0] == '.') {
      if (strcmp(all_files[i].name, "..") != 0) {
        continue;
      }
    }

    /* Explicitly filter out TRASH folder if attributes didn't catch it */
    if (strncmp(all_files[i].name, "TRASH-~1", 8) == 0) {
      continue;
    }

    /* Show directories and .WAV files only */
    if (all_files[i].is_dir || str_ends_with(all_files[i].name, ".WAV")) {
      if (file_count < FAT32_MAX_FILES) {
        file_list[file_count] = all_files[i];
        file_count++;
      }
    }
  }

  if (file_count < 0)
    file_count = 0;
}

static void DrawChannelEditScreen(uint8_t full_redraw) {
  if (full_redraw) {
    ST7789_Fill(BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "CH %d EDIT", selected_channel + 1);
    ST7789_WriteString(10, 10, buf, YELLOW, BLACK, 2);
  }

  if (is_channel_edit_mode == 1 || is_channel_edit_mode == 3 ||
      is_channel_edit_mode == 4) {
    char buf[32];

    /* Determine which rows to draw */
    int draw_r0 = full_redraw;
    int draw_r1 = full_redraw;
    int draw_r2 = full_redraw;

    /* Mode 1: Menu Navigation */
    if (is_channel_edit_mode == 1 && !full_redraw) {
      if (edit_menu_index != last_menu_index) {
        if (last_menu_index == 0 || edit_menu_index == 0)
          draw_r0 = 1;
        if (last_menu_index == 1 || edit_menu_index == 1)
          draw_r1 = 1;
        if (last_menu_index == 2 || edit_menu_index == 2)
          draw_r2 = 1;
      }
    }

    /* Mode 3: Vol Edit - Only update R1 */
    if (is_channel_edit_mode == 3) {
      draw_r1 = 1;
      draw_r0 = 0;
      draw_r2 = 0;
    }

    /* Mode 4: Pan Edit - Only update R2 */
    if (is_channel_edit_mode == 4) {
      draw_r2 = 1;
      draw_r0 = 0;
      draw_r1 = 0;
    }

    int highlight_row = -1;
    if (is_channel_edit_mode == 1)
      highlight_row = edit_menu_index;

    /* Row 0: Sample Name */
    if (draw_r0) {
      uint16_t c0 = (highlight_row == 0) ? WHITE : GRAY;
      uint16_t bg0 = (highlight_row == 0) ? DARKBLUE : BLACK;
      ST7789_FillRect(0, 40, 240, 30, bg0);
      snprintf(buf, sizeof(buf), "SMP: %s",
               current_drumset->sample_names[selected_channel]);
      ST7789_WriteString(10, 48, buf, c0, bg0, 2);
    }

    /* Row 1: Volume */
    if (draw_r1) {
      uint16_t c1 = (highlight_row == 1) ? WHITE : GRAY;
      uint16_t bg1 = (highlight_row == 1) ? DARKBLUE : BLACK;
      if (is_channel_edit_mode == 3) {
        c1 = RED;
        bg1 = BLACK;
      }

      /* Only fill background if not actively editing (to prevent flicker) */
      if (is_channel_edit_mode != 3 || full_redraw) {
        ST7789_FillRect(0, 80, 240, 30, bg1);
      }

      uint8_t vol = current_drumset->volumes[selected_channel];
      snprintf(buf, sizeof(buf), "VOL: %d   ", vol);
      ST7789_WriteString(10, 88, buf, c1, bg1, 2);
      /* Bar Graph (Split Fill for Flicker-Free Update) */
      ST7789_DrawThickFrame(130, 85, 100, 20, 1, c1);
      int bar_w = (vol * 96) / 255;
      /* Draw filled part */
      ST7789_FillRect(132, 87, bar_w, 16, c1);
      /* Draw empty part */
      ST7789_FillRect(132 + bar_w, 87, 96 - bar_w, 16, bg1);
    }

    /* Row 2: Pan */
    if (draw_r2) {
      uint16_t c2 = (highlight_row == 2) ? WHITE : GRAY;
      uint16_t bg2 = (highlight_row == 2) ? DARKBLUE : BLACK;
      if (is_channel_edit_mode == 4) {
        c2 = RED;
        bg2 = BLACK;
      }

      /* Only fill background if not actively editing (to prevent flicker) */
      if (is_channel_edit_mode != 4 || full_redraw) {
        ST7789_FillRect(0, 120, 240, 30, bg2);
      }

      uint8_t pan = current_drumset->pans[selected_channel];
      char pan_char = 'C';
      if (pan < 120)
        pan_char = 'L';
      if (pan > 136)
        pan_char = 'R';
      snprintf(buf, sizeof(buf), "PAN: %c %d   ", pan_char, pan);
      ST7789_WriteString(10, 128, buf, c2, bg2, 2);

      /* Pan Graph (3-Chunk Fill for Flicker-Free Update) */
      ST7789_DrawThickFrame(130, 125, 100, 20, 1, c2);
      int x_pan = 132 + ((pan * 96) / 255);
      int cursor_w = 4;
      int x_start = 132;
      int width = 96;

      /* Left BG */
      if (x_pan - 2 > x_start) {
        ST7789_FillRect(x_start, 127, (x_pan - 2) - x_start, 16, bg2);
      }
      /* Cursor */
      ST7789_FillRect(x_pan - 2, 127, cursor_w, 16,
                      (is_channel_edit_mode == 4) ? RED : c2);
      /* Right BG */
      if (x_pan + 2 < x_start + width) {
        ST7789_FillRect(x_pan + 2, 127, (x_start + width) - (x_pan + 2), 16,
                        bg2);
      }

      /* Redraw Center Marker if not covered by cursor */
      if (x_pan - 2 > 180 || x_pan + 2 < 180) {
        ST7789_DrawVLine(180, 125, 20, c2);
      }
    }

    last_menu_index = edit_menu_index;

  } else if (is_channel_edit_mode == 2) {
    /* FILE BROWSER */
    if (full_redraw) {
      ST7789_WriteString(150, 10, "BROWSE", GREEN, BLACK, 2);
    }

    for (int i = 0; i < 8 && i < file_count; i++) {
      /* Optimize: Only redraw if full_redraw OR if this row changed selection
       * state */
      int is_selected = (i == selected_file_index);
      int was_selected = (i == last_selected_file_index);

      if (full_redraw || is_selected != was_selected ||
          (full_redraw == 0 &&
           (i == selected_file_index || i == last_selected_file_index))) {
        /* Directories in yellow, files in white/gray */
        uint16_t color =
            file_list[i].is_dir ? YELLOW : (is_selected ? WHITE : GRAY);
        uint16_t y_pos = 40 + (i * 20);

        if (is_selected) {
          ST7789_FillRect(0, y_pos, 240, 20, DARKBLUE);
        } else {
          ST7789_FillRect(0, y_pos, 240, 20, BLACK);
        }
        ST7789_WriteString(10, y_pos, file_list[i].name, color,
                           is_selected ? DARKBLUE : BLACK, 2);
      }
    }
    last_selected_file_index = selected_file_index;
  }
}

static void DrawDrumsetMenu(uint8_t full_redraw) {
  if (full_redraw) {
    ST7789_Fill(BLACK);
  }

  if (is_drumset_menu_mode == 1) {
    /* Main Menu */
    ST7789_WriteString(10, 10, "DRUMSET MENU", YELLOW, BLACK, 2);

    const char *menu_items[] = {"LOAD", "SAVE", "BACK"};
    for (int i = 0; i < 3; i++) {
      uint16_t y_pos = 60 + (i * 40);
      uint16_t color = (i == drumset_menu_index) ? WHITE : GRAY;

      ST7789_WriteString(10, y_pos, (i == drumset_menu_index) ? ">" : " ",
                         YELLOW, BLACK, 2);
      ST7789_WriteString(40, y_pos, menu_items[i], color, BLACK, 2);
    }
  } else if (is_drumset_menu_mode == 2) {
    /* Save Slots */
    ST7789_WriteString(10, 10, "SAVE KIT", YELLOW, BLACK, 2);

    /* Display 8 slots in a stable window (starts at start_slot) */
    int start_slot = ((selected_slot - 1) / 8) * 8 + 1;
    if (start_slot > 93)
      start_slot = 93;

    for (int i = 0; i < 8; i++) {
      uint8_t slot_num = start_slot + i;
      uint16_t y_pos = 50 + (i * 20);
      char slot_text[20];

      if (slot_num <= 100) {
        /* Check if occupied */
        int is_occupied = 0;
        for (int j = 0; j < occupied_slot_count; j++) {
          if (occupied_slots[j] == slot_num) {
            is_occupied = 1;
            break;
          }
        }

        /* 16 chars max to avoid wrapping at x=40 (40 + 16*12 = 232 < 240) */
        snprintf(slot_text, sizeof(slot_text), "Kit-%03d %s  ", slot_num,
                 is_occupied ? "[X]" : "   ");

        uint16_t color = (slot_num == selected_slot) ? WHITE : GRAY;
        ST7789_WriteString(10, y_pos, (slot_num == selected_slot) ? ">" : " ",
                           YELLOW, BLACK, 2);
        ST7789_WriteString(40, y_pos, slot_text, color, BLACK, 2);
      } else {
        /* Blank out row securely */
        ST7789_WriteString(10, y_pos, "                ", BLACK, BLACK, 2);
        ST7789_WriteString(40, y_pos, "                ", BLACK, BLACK, 2);
      }
    }
  } else if (is_drumset_menu_mode == 3) {
    /* Load Slots - only show occupied */
    ST7789_WriteString(10, 10, "LOAD KIT", YELLOW, BLACK, 2);

    if (occupied_slot_count == 0) {
      ST7789_WriteString(40, 100, "NO SAVED KITS", GRAY, BLACK, 2);
    } else {
      /* Find current selection index in occupied list */
      int current_idx = 0;
      for (int i = 0; i < occupied_slot_count; i++) {
        if (occupied_slots[i] == selected_slot) {
          current_idx = i;
          break;
        }
      }

      /* Use stable window scrolling for LOAD menu as well */
      int start_idx = (current_idx / 8) * 8;

      /* Always draw 8 rows to clean up artifacts */
      for (int i = 0; i < 8; i++) {
        int idx = start_idx + i;
        uint16_t y_pos = 50 + (i * 20);

        if (idx < occupied_slot_count) {
          uint8_t slot_num = occupied_slots[idx];
          char slot_text[20];
          /* 16 chars max */
          snprintf(slot_text, sizeof(slot_text), "Kit-%03d [X]  ", slot_num);

          uint16_t color = (slot_num == selected_slot) ? WHITE : GRAY;
          ST7789_WriteString(10, y_pos, (slot_num == selected_slot) ? ">" : " ",
                             YELLOW, BLACK, 2);
          ST7789_WriteString(40, y_pos, slot_text, color, BLACK, 2);
        } else {
          /* Blank out invalid row positions securely */
          ST7789_WriteString(10, y_pos, "                ", BLACK, BLACK, 2);
          ST7789_WriteString(40, y_pos, "                ", BLACK, BLACK, 2);
        }
      }
    }
  }
}

static void TriggerChannelEdit(void) {
  is_channel_edit_mode = 1; /* Go to Menu */
  edit_menu_index = 0;
  last_menu_index = 0;
  Encoder_SetLimits(0, 2); /* 3 Menu Items */
  Encoder_SetValue(0);
  Encoder_ResetIncrement();
  mode_changed = 1;
  full_redraw_needed = 1;
}
static void ExitChannelEdit(void) {
  is_channel_edit_mode = 0;
  /* Restore Drumset Edit config */
  Encoder_SetLimits(0, NUM_CHANNELS - 1);
  Encoder_SetValue(selected_channel);
  mode_changed = 1;
  full_redraw_needed = 1;
}
static void ExitDrumsetMenu(void) {
  is_drumset_menu_mode = 0;

  if (is_edit_mode) {
    Encoder_SetLimits(0, NUM_CHANNELS - 1);
    Encoder_SetValue(selected_channel);
  } else {
    Encoder_SetLimits(40, 300);
    Encoder_SetValue(Sequencer_GetBPM());
  }

  full_redraw_needed = 1;
  mode_changed = 1;
}

static void ExitPatternMenu(void) {
  is_pattern_menu_mode = 0;
  if (is_pattern_edit_mode) {
    if (is_pattern_detail_mode) {
      Encoder_SetLimits(0, 31);
      Encoder_SetValue(pattern_cursor);
    } else {
      Encoder_SetLimits(0, NUM_CHANNELS - 1);
      Encoder_SetValue(selected_channel);
    }
  } else {
    Encoder_SetLimits(40, 300);
    Encoder_SetValue(Sequencer_GetBPM());
  }
  full_redraw_needed = 1;
  mode_changed = 1;
}

static void DrawPatternMenu(uint8_t full_redraw) {
  if (full_redraw) {
    ST7789_Fill(BLACK);
  }

  if (is_pattern_menu_mode == 1) {
    /* Main Menu */
    ST7789_WriteString(10, 10, "PATTERN MENU", CYAN, BLACK, 2);

    const char *menu_items[] = {"LOAD", "SAVE", "BACK"};
    for (int i = 0; i < 3; i++) {
      uint16_t y_pos = 60 + (i * 40);
      uint16_t color = (i == pattern_menu_index) ? WHITE : GRAY;

      ST7789_WriteString(10, y_pos, (i == pattern_menu_index) ? ">" : " ", CYAN,
                         BLACK, 2);
      ST7789_WriteString(40, y_pos, menu_items[i], color, BLACK, 2);
    }
  } else if (is_pattern_menu_mode == 2) {
    /* Save Slots */
    ST7789_WriteString(10, 10, "SAVE PATTERN", CYAN, BLACK, 2);

    int start_slot = ((selected_slot - 1) / 8) * 8 + 1;
    if (start_slot > 93)
      start_slot = 93;

    for (int i = 0; i < 8; i++) {
      uint8_t slot_num = start_slot + i;
      uint16_t y_pos = 50 + (i * 20);
      char slot_text[20];

      if (slot_num <= 100) {
        int is_occupied = 0;
        for (int j = 0; j < occupied_slot_count; j++) {
          if (occupied_slots[j] == slot_num) {
            is_occupied = 1;
            break;
          }
        }

        snprintf(slot_text, sizeof(slot_text), "Pat-%03d %s  ", slot_num,
                 is_occupied ? "[X]" : "   ");

        uint16_t color = (slot_num == selected_slot) ? WHITE : GRAY;
        ST7789_WriteString(10, y_pos, (slot_num == selected_slot) ? ">" : " ",
                           CYAN, BLACK, 2);
        ST7789_WriteString(40, y_pos, slot_text, color, BLACK, 2);
      } else {
        ST7789_WriteString(10, y_pos, "                ", BLACK, BLACK, 2);
        ST7789_WriteString(40, y_pos, "                ", BLACK, BLACK, 2);
      }
    }
  } else if (is_pattern_menu_mode == 3) {
    /* Load Slots */
    ST7789_WriteString(10, 10, "LOAD PATTERN", CYAN, BLACK, 2);

    if (occupied_slot_count == 0) {
      ST7789_WriteString(40, 100, "NO SAVED PATS", GRAY, BLACK, 2);
    } else {
      int current_idx = 0;
      for (int i = 0; i < occupied_slot_count; i++) {
        if (occupied_slots[i] == selected_slot) {
          current_idx = i;
          break;
        }
      }

      int start_idx = (current_idx / 8) * 8;
      for (int i = 0; i < 8; i++) {
        int idx = start_idx + i;
        uint16_t y_pos = 50 + (i * 20);
        char slot_text[20];

        if (idx < occupied_slot_count) {
          uint8_t slot_num = occupied_slots[idx];
          snprintf(slot_text, sizeof(slot_text), "Pat-%03d      ", slot_num);

          uint16_t color = (slot_num == selected_slot) ? WHITE : GRAY;
          ST7789_WriteString(10, y_pos, (slot_num == selected_slot) ? ">" : " ",
                             CYAN, BLACK, 2);
          ST7789_WriteString(40, y_pos, slot_text, color, BLACK, 2);
        } else {
          ST7789_WriteString(10, y_pos, "                ", BLACK, BLACK, 2);
          ST7789_WriteString(40, y_pos, "                ", BLACK, BLACK, 2);
        }
      }
    }
  }
}

static void ToggleEditMode(void) {

  is_edit_mode = !is_edit_mode;
  if (is_edit_mode) {
    saved_bpm = Encoder_GetValue();
    Encoder_SetLimits(0, NUM_CHANNELS - 1);
    Encoder_SetValue(selected_channel);
    Encoder_ResetIncrement();
  } else {
    selected_channel = Encoder_GetValue();
    Encoder_SetLimits(40, 300);
    Encoder_SetValue(saved_bpm);
  }
  mode_changed = 1;
}

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

  /* Configure SysTick for 1ms (assuming 96MHz HCLK) */
  STK_LOAD = 96000 - 1;
  STK_VAL = 0;
  STK_CTRL = (1 << 0) | (1 << 1) | (1 << 2); /* ENABLE, TICKINT, CLKSOURCE */

  /* --- Configure Interrupt Priorities --- */
  /* DMA1 Stream 4 (Audio): IRQ 15 */
  NVIC_IPR_BASE[15] = (0 << 4); /* Highest Priority - Audio Refill */
  /* TIM2 (Sequencer Clock): IRQ 28 */
  NVIC_IPR_BASE[28] = (1 << 4); /* High Priority - Rhythmic Timing */
  /* EXTI lines (Buttons/Encoder): IRQ 6, 7, 23 */
  NVIC_IPR_BASE[6] = (3 << 4); /* Lower Priority */
  NVIC_IPR_BASE[7] = (3 << 4); /* Lower Priority */
  /* TIM5 (Button Debounce/OnButtonEvent): IRQ 50 */
  NVIC_IPR_BASE[50] =
      (3 << 4); /* Lower Priority - Heavy SD operations happen here */
  NVIC_IPR_BASE[23] = (3 << 4); /* Lower Priority */

  /* SD initialization and sample auto-load (Slot 1) */
  (void)FAT32_Init();
  static Drumset drumset;
  memset(&drumset, 0, sizeof(Drumset));
  for (int i = 0; i < NUM_CHANNELS; i++) {
    strcpy(drumset.sample_names[i], "EMPTY");
    drumset.volumes[i] = 255;
    drumset.pans[i] = 127;
  }
  current_drumset = &drumset;

  /* Attempt to load Slot 1 on boot */
  if (Drumset_LoadFromSlot(&drumset, 1) != 0) {
    /* If failed, set default name */
    strcpy(drumset.name, "KIT-001");
  }

  /* Note: Drumset_LoadFromSlot handles AudioMixer_SetSample/Vol/Pan internally.
   * No further manual assignment is needed here, matching the manual load
   * logic.
   */

  /* Start audio subsystem as early as possible for PLLI2S stability */
  int audio_status = I2S_Init();
  if (audio_status == 0) {
    for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
      audio_buffer[i] = 0;
    DMA_Init_I2S(audio_buffer, AUDIO_BUFFER_SIZE);
    I2S_Start();
  }

  /* Attempt to load Pattern Slot 1 on boot */
  Pattern *boot_pat = Sequencer_GetPattern();
  uint16_t default_bpm = 120;
  if (Pattern_Load(boot_pat, 1) == 0) {
    loaded_pattern_slot = 1;
    /* Force default 120 BPM regardless of file content */
    boot_pat->bpm = default_bpm;
    Sequencer_SetBPM(default_bpm);
  } else {
    /* Fallback to Test Pattern if no SD pattern found */
    LoadTestPattern();
    Sequencer_SetBPM(default_bpm);
  }

  /* Ensure Encoder matches the default 120 */
  Encoder_SetValue(default_bpm);
  DrawMainScreen(&drumset);

  int32_t last_encoder = 0;
  int32_t last_increment = 0;
  uint32_t channel_blink_times[NUM_CHANNELS] = {0}; // For sequencer blinkers

  while (1) {
    Button_HandleEvents();

    /* Handle Mode Change */
    if (mode_changed) {
      mode_changed = 0;
      last_encoder = Encoder_GetValue();

      if (full_redraw_needed) {
        if (is_channel_edit_mode) {
          DrawChannelEditScreen(1);
        } else if (is_drumset_menu_mode) {
          DrawDrumsetMenu(1);
        } else if (is_pattern_menu_mode) {
          DrawPatternMenu(1);
        } else if (is_pattern_detail_mode) {
          DrawStepEditScreen(1);
        } else {
          DrawMainScreen(current_drumset);
        }
        full_redraw_needed = 0;
      } else {
        /* Incremental update */
        if (is_channel_edit_mode) {
          DrawChannelEditScreen(0);
        } else if (is_drumset_menu_mode) {
          DrawDrumsetMenu(0);
        } else if (is_pattern_menu_mode) {
          DrawPatternMenu(0);
        } else if (is_pattern_detail_mode) {
          DrawStepEditScreen(0);
        } else {
          UpdateModeUI();
        }
      }
    }

    /* Handle Async Popup (Success/Error) */
    if (is_ui_popup) {
      if (HAL_GetTick() - ui_popup_start_time > 1200) {
        is_ui_popup = 0;

        /* Handle automatic menu exit on success if requested */
        if (ui_popup_exit_type == 1) {
          ExitDrumsetMenu();
        } else if (ui_popup_exit_type == 2) {
          ExitPatternMenu();
        } else {
          full_redraw_needed = 1; /* Just restore screen from popup */
          mode_changed = 1;
        }
        ui_popup_exit_type = 0;
      }
    }

    /* Long-press detection for Drumset Menu */
    if (button_drumset_pressed && !button_drumset_handled &&
        !is_drumset_menu_mode && !is_pattern_edit_mode) {
      if (HAL_GetTick() - button_drumset_start_time >= 500) {
        /* Long-press (0.5s) detected */
        is_drumset_menu_mode = 1;
        drumset_menu_index = 0;
        Encoder_SetLimits(0, 2);
        Encoder_SetValue(0);
        DrawDrumsetMenu(1); /* Full redraw on entry */
        button_drumset_handled = 1;
      }
    }

    /* Long-press detection for Pattern Menu */
    if (button_pattern_pressed && !button_pattern_handled &&
        !is_pattern_menu_mode && !is_drumset_menu_mode) {
      if (HAL_GetTick() - button_pattern_start_time >= 500) {
        /* Long-press (0.5s) detected */
        is_pattern_menu_mode = 1;
        pattern_menu_index = 0;
        Encoder_SetLimits(0, 2);
        Encoder_SetValue(0);
        DrawPatternMenu(1); /* Full redraw on entry */
        button_pattern_handled = 1;
      }
    }

    /* Robust EDIT button release detection by polling GPIO (PB9 is Bit 9) */
    if (button_drumset_pressed) {
      uint8_t current_val = (GPIOB_IDR & (1 << 9)) ? 1 : 0;
      if (current_val == 1) { /* Physcially released (Pull-up) */
        button_drumset_pressed = 0;
        if (!button_drumset_handled && !is_drumset_menu_mode &&
            !is_pattern_edit_mode) {
          /* Short press (Click) detected - toggle edit mode */
          ToggleEditMode();
        }
      }
    }

    /* Robust PATTERN button release detection by polling GPIO (PB1 is Bit 1) */
    if (button_pattern_pressed) {
      uint8_t current_val = (GPIOB_IDR & (1 << 1)) ? 1 : 0;
      if (current_val == 1) { /* Physcially released (Pull-up) */
        button_pattern_pressed = 0;
        if (!button_pattern_handled && !is_pattern_menu_mode) {
          /* Short press (Click) detected - handle normal pattern toggle */
          /* We simulate a button event call here or handle logic directly */
          /* To keep it clean, handle the logic that was in OnButtonEvent but at
           * release */

          /* Toggle Pattern Edit Mode - Block if in Drumset Edit or other menus
           */
          if (!is_drumset_menu_mode && !is_channel_edit_mode && !is_edit_mode) {
            if (is_pattern_detail_mode) {
              /* Quick return to Grid Mode from Step Edit */
              is_pattern_detail_mode = 0;
              Encoder_SetLimits(0, NUM_CHANNELS - 1);
              Encoder_SetValue(selected_channel);
              Encoder_ResetIncrement();
              full_redraw_needed = 1;
              mode_changed = 1;
            } else {
              is_pattern_edit_mode = !is_pattern_edit_mode;
              mode_changed = 1;

              if (is_pattern_edit_mode) {
                /* Clear any active playback highlights */
                for (int i = 0; i < NUM_CHANNELS; i++) {
                  if (channel_states[i]) {
                    UpdateBlinker(i, 0);
                    channel_states[i] = 0;
                  }
                }
                Encoder_SetLimits(0, NUM_CHANNELS - 1);
                Encoder_SetValue(selected_channel);
                Encoder_ResetIncrement();
                UpdateBlinker(selected_channel,
                              1); /* Ensure selection is visible */
              } else {
                /* Return to previous limits */
                if (is_edit_mode) {
                  Encoder_SetLimits(0, NUM_CHANNELS - 1);
                  Encoder_SetValue(selected_channel);
                } else {
                  Encoder_SetLimits(40, 300);
                  Encoder_SetValue(Sequencer_GetBPM());
                }
              }
            }
          }
        }
      }
    }

    /* Handle BPM/Channel updates from encoder */
    int32_t encoder_val = Encoder_GetValue();
    if (encoder_val != last_encoder) {
      last_encoder = encoder_val;

      if (is_drumset_menu_mode == 1) {
        /* Drumset menu navigation */
        drumset_menu_index = encoder_val;
        DrawDrumsetMenu(0); /* Partial redraw */
      } else if (is_drumset_menu_mode == 2) {
        /* Save slot selection */
        selected_slot = (uint8_t)encoder_val;
        DrawDrumsetMenu(0); /* Partial redraw */
      } else if (is_drumset_menu_mode == 3) {
        /* Load slot selection - encoder value is index in occupied_slots */
        if (encoder_val >= 0 && encoder_val < occupied_slot_count) {
          selected_slot = occupied_slots[encoder_val];
          DrawDrumsetMenu(0); /* Partial redraw */
        }
      } else if (is_channel_edit_mode == 1) {
        edit_menu_index = encoder_val;
        DrawChannelEditScreen(0);
      } else if (is_channel_edit_mode == 2) {
        selected_file_index = encoder_val;
        DrawChannelEditScreen(0);
      } else if (is_channel_edit_mode == 3) {
        /* Volume Edit */
        current_drumset->volumes[selected_channel] = (uint8_t)encoder_val;
        AudioMixer_SetVolume(selected_channel, (uint8_t)encoder_val);
        DrawChannelEditScreen(0);
      } else if (is_channel_edit_mode == 4) {
        /* Pan Edit */
        current_drumset->pans[selected_channel] = (uint8_t)encoder_val;
        AudioMixer_SetPan(selected_channel, (uint8_t)encoder_val);
        DrawChannelEditScreen(0);
      } else if (is_pattern_menu_mode == 1) {
        pattern_menu_index = encoder_val;
        DrawPatternMenu(0);
      } else if (is_pattern_menu_mode == 2) {
        selected_slot = (uint8_t)encoder_val;
        DrawPatternMenu(0);
      } else if (is_pattern_menu_mode == 3) {
        if (encoder_val >= 0 && encoder_val < occupied_slot_count) {
          selected_slot = occupied_slots[encoder_val];
          DrawPatternMenu(0);
        }
      } else if (is_pattern_detail_mode) {
        pattern_cursor = (int8_t)encoder_val;
        DrawStepEditScreen(0); /* Incremental redraw of step cursor */
      } else if (is_edit_mode || is_pattern_edit_mode) {
        /* Handle Channel Selection in both Normal Edit and Pattern Edit */
        selected_channel = (uint8_t)encoder_val;
        mode_changed = 1; // Trigger UI update for channel highlight
      } else {
        /* Handle BPM Change */
        Sequencer_SetBPM((uint16_t)encoder_val);

        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%d ", (int)encoder_val);
        uint16_t val_color =
            (Encoder_GetIncrementStep() == 10) ? MAGENTA : WHITE;
        ST7789_WriteString(10, 10, "BPM:", WHITE, BLACK, 2);
        ST7789_WriteString(60, 10, val_buf, val_color, BLACK, 2);
      }
    }

    /* Handle Step Toggling from Button Event - GUARD: but not while in menu */
    if (needs_step_update) {
      needs_step_update = 0;
      if (!is_drumset_menu_mode && !is_pattern_menu_mode &&
          !full_redraw_needed) {
        DrawStepEditScreen(
            3); // Force redraw of current cursor cell for velocity change
      }
    }

    /* UI guards for background updates while menus are active */
    if (!is_drumset_menu_mode && !is_channel_edit_mode &&
        !is_pattern_menu_mode && !full_redraw_needed) {
      /* Handle UI refresh when playback stops */
      if (needs_ui_refresh) {
        needs_ui_refresh = 0;
        last_step = 0xFF;

        /* Reset STEP counter display */
        char step_buf[32];
        snprintf(step_buf, sizeof(step_buf), "01/%02d",
                 Sequencer_GetStepCount());
        ST7789_WriteString(255, 10, step_buf, WHITE, BLACK, 2);

        /* Reset any active blinkers without full screen redraw */
        for (int i = 0; i < NUM_CHANNELS; i++) {
          if (channel_states[i]) {
            if (!is_pattern_edit_mode) {
              UpdateBlinker(i, 0);
            }
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

      /* Update BPM if changed (from external means or sync) */
      int32_t increment =
          Encoder_GetIncrementStep(); // Use GetIncrementStep for UI display
      if (increment != last_increment) {
        last_increment = increment;
        char val_buf[16];
        snprintf(val_buf, sizeof(val_buf), "%d ", (int)Encoder_GetValue());
        uint16_t val_color = (increment == 10) ? MAGENTA : WHITE;
        ST7789_WriteString(10, 10, "BPM:", WHITE, BLACK, 2);
        ST7789_WriteString(60, 10, val_buf, val_color, BLACK, 2);
      }

      /* Sequencer animation */
      if (is_playing) {
        uint8_t step = Sequencer_GetCurrentStep();
        if (step != last_step) {
          char buf[32];
          snprintf(buf, sizeof(buf), "%02d/%02d", step + 1,
                   Sequencer_GetStepCount());
          ST7789_WriteString(255, 10, buf, WHITE, BLACK, 2);

          for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
            uint8_t velocity = Sequencer_GetStep(i, step);
            if (velocity > 0) {
              if (!is_pattern_edit_mode) {
                UpdateBlinker(i, 1);
              }
              channel_states[i] = 1;
              channel_blink_times[i] =
                  HAL_GetTick(); // Record time for blink duration
            }
          }

          /* Update Step Edit screen playhead if active AND not in menu */
          if (is_pattern_detail_mode && !is_pattern_menu_mode &&
              !is_drumset_menu_mode) {
            DrawStepEditScreen(0);
          }

          /* LED Blink on quarter notes */
          if ((step % 4) == 0) {
            GPIOC_ODR &= ~(1 << 13); /* ON */
          } else {
            GPIOC_ODR |= (1 << 13); /* OFF */
          }
          last_step = step;
        }

        // Turn off blinkers after a short duration
        for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
          if (channel_states[i] &&
              (HAL_GetTick() - channel_blink_times[i] > 100)) {
            /* Guard: Don't unhighlight the manual selection in Edit Mode */
            if (!is_pattern_edit_mode &&
                !(is_edit_mode && i == selected_channel)) {
              UpdateBlinker(i, 0);
            }
            channel_states[i] = 0;
          }
        }
      }
      /* Handle Queued Pattern UI and detection */
      static uint8_t last_queued_state = 0;
      static uint8_t blink_on = 1;
      static uint32_t last_blink_time = 0;
      uint8_t current_queued_state = Sequencer_IsPatternQueued();

      if (current_queued_state != last_queued_state) {
        if (last_queued_state == 1 && current_queued_state == 0) {
          /* Queue was just applied by sequencer rollover */
          loaded_pattern_slot = Sequencer_GetQueuedSlot();

          if (is_pattern_edit_mode || is_edit_mode || is_channel_edit_mode) {
            /* Return to main screen and redraw fully */
            is_pattern_edit_mode = 0;
            is_pattern_detail_mode = 0;
            is_edit_mode = 0;
            is_channel_edit_mode = 0;
            full_redraw_needed = 1;
            mode_changed = 1;
          } else {
            /* Already on main screen: Flicker-free header update only */
            last_bpm = 0xFF; /* Force header refresh in UpdateModeUI */
            /* Stop blinking and show solid Pattern ID */
            char pat_buf[16];
            snprintf(pat_buf, sizeof(pat_buf), "P-%03d", loaded_pattern_slot);
            ST7789_WriteString(170, 10, pat_buf, YELLOW, BLACK, 2);
          }
          blink_on = 1;
        } else if (last_queued_state == 0 && current_queued_state == 1) {
          /* New pattern just queued */
          blink_on = 1;
          last_blink_time = HAL_GetTick();
        }
        last_queued_state = current_queued_state;
      }

      if (current_queued_state) {
        /* Blink Pattern ID - Twice as fast (125ms) */
        if (HAL_GetTick() - last_blink_time > 125) {
          blink_on = !blink_on;
          last_blink_time = HAL_GetTick();

          char pat_buf[16];
          snprintf(pat_buf, sizeof(pat_buf), "P-%03d",
                   Sequencer_GetQueuedSlot());
          if (blink_on) {
            ST7789_WriteString(170, 10, pat_buf, YELLOW, BLACK, 2);
          } else {
            /* Clear text area */
            ST7789_WriteString(170, 10, "      ", BLACK, BLACK, 2);
          }
        }
      }

      if (needs_full_grid_update) {
        needs_full_grid_update = 0;
        if (is_pattern_edit_mode) {
          DrawStepEditScreen(2); /* Flicker-free full grid refresh */
        }
      }
    }

    __asm volatile("wfi");
  }
}

static void LoadTestPattern(void) {
  /* Clear pattern */
  Sequencer_ClearPattern();

  /* Set 32 Steps (2 Bars) */
  Sequencer_SetStepCount(32);

  /* KICK (Ch 0): 4-on-the-floor (Same for both bars) */
  for (int i = 0; i < 32; i += 4) {
    Sequencer_SetStep(0, i, 200);
  }

  /* SNARE (Ch 1): Backbeat on 2 and 4 (Steps 4, 12, 20, 28) */
  Sequencer_SetStep(1, 4, 255);
  Sequencer_SetStep(1, 12, 255);
  Sequencer_SetStep(1, 20, 255);
  Sequencer_SetStep(1, 28, 255);
  /* Snare Variation: Ghost at end of Bar 2 */
  Sequencer_SetStep(1, 31, 80);

  /* HATS (Ch 2): Off-beat Open Hat (2, 6, 10, 14...) */
  for (int i = 2; i < 32; i += 4) {
    Sequencer_SetStep(2, i, 180);
  }
  /* Hats Variation: 16th notes in Bar 2 fill */
  Sequencer_SetStep(2, 29, 100);
  Sequencer_SetStep(2, 31, 100);

  /* CLAP (Ch 3): Dub Echo - Bar 1 Only */
  Sequencer_SetStep(3, 4, 200);
  Sequencer_SetStep(3, 7, 60);
  Sequencer_SetStep(3, 10, 30);
  /* Clap Variation: Bar 2 response (sparser) */
  Sequencer_SetStep(3, 20, 200);

  /* PERC 1 (Ch 4): Glitch Texture */
  Sequencer_SetStep(4, 11, 150);
  Sequencer_SetStep(4, 15, 100);
  Sequencer_SetStep(4, 27, 150);
  Sequencer_SetStep(4, 30, 120);

  /* PERC 2 (Ch 5): Deep Texture */
  Sequencer_SetStep(5, 0, 80);
  Sequencer_SetStep(5, 16, 100); // Start of Bar 2 accent
}

static void DrawMainScreen(Drumset *drumset) {
  ST7789_Fill(BLACK);

  if (is_pattern_edit_mode) {
    ST7789_WriteString(10, 10, "PATTERN EDIT ", CYAN, BLACK, 2);
  } else if (is_edit_mode) {
    ST7789_WriteString(10, 10, "DRUMSET EDIT ", YELLOW, BLACK, 2);
  } else {
    ST7789_WriteString(10, 10, "BPM:", WHITE, BLACK, 2);
    char val_buf[16];
    snprintf(val_buf, sizeof(val_buf), "%d", (int)Encoder_GetValue());
    ST7789_WriteString(60, 10, val_buf, WHITE, BLACK, 2);
  }

  char step_buf[32];
  snprintf(step_buf, sizeof(step_buf), "01/%02d", Sequencer_GetStepCount());
  ST7789_WriteString(255, 10, step_buf, WHITE, BLACK, 2);

  /* Pattern Info (Center, Yellow) */
  if (loaded_pattern_slot > 0) {
    char pat_buf[16];
    snprintf(pat_buf, sizeof(pat_buf), "P-%03d", loaded_pattern_slot);
    ST7789_WriteString(170, 10, pat_buf, YELLOW, BLACK, 2);
  }

  /* Status indicator - Always show PLAY/STOP for clarity */
  const char *status = is_playing ? "PLAYING      " : "STOPPED      ";
  uint16_t status_color = is_playing ? GREEN : RED;
  ST7789_WriteString(10, 220, status, status_color, BLACK, 2);

  /* Show Loaded Kit Name in Footer (Right Aligned, Yellow) */
  ST7789_WriteString(230, 220, drumset->name, WHITE, BLACK, 2);

  /* 3x2 Grid Layout
   * Width 90px, Height 80px
   * Row 1 Y=40, Row 2 Y=130
   * Col 1 X=10, Col 2 X=110, Col 3 X=210
   */

  /* Channel 0: Red */
  ST7789_FillRect(10, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(10, 40, 90, 80, 2, RED);
  ST7789_WriteString(15, 50, drumset->sample_names[0], RED, BLACK, 1);
  ST7789_WriteString(85, 105, "1", RED, BLACK, 1); /* Channel number */

  /* Channel 1: Green */
  ST7789_FillRect(110, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(110, 40, 90, 80, 2, GREEN);
  ST7789_WriteString(115, 50, drumset->sample_names[1], GREEN, BLACK, 1);
  ST7789_WriteString(185, 105, "2", GREEN, BLACK, 1); /* Channel number */

  /* Channel 2: Yellow */
  ST7789_FillRect(210, 40, 90, 80, BLACK);
  ST7789_DrawThickFrame(210, 40, 90, 80, 2, YELLOW);
  ST7789_WriteString(215, 50, drumset->sample_names[2], YELLOW, BLACK, 1);
  ST7789_WriteString(285, 105, "3", YELLOW, BLACK, 1); /* Channel number */

  /* Channel 3: Magenta */
  ST7789_FillRect(10, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(10, 130, 90, 80, 2, MAGENTA);
  ST7789_WriteString(15, 140, drumset->sample_names[3], MAGENTA, BLACK, 1);
  ST7789_WriteString(85, 195, "4", MAGENTA, BLACK, 1); /* Channel number */

  /* Channel 4: Cyan */
  ST7789_FillRect(110, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(110, 130, 90, 80, 2, CYAN);
  ST7789_WriteString(115, 140, drumset->sample_names[4], CYAN, BLACK, 1);
  ST7789_WriteString(185, 195, "5", CYAN, BLACK, 1); /* Channel number */

  /* Channel 5: Orange */
  ST7789_FillRect(210, 130, 90, 80, BLACK);
  ST7789_DrawThickFrame(210, 130, 90, 80, 2, ORANGE);
  ST7789_WriteString(215, 140, drumset->sample_names[5], ORANGE, BLACK, 1);
  ST7789_WriteString(285, 195, "6", ORANGE, BLACK, 1); /* Channel number */

  /* Highlight selected channel if in edit mode or pattern mode */
  if (is_edit_mode || is_pattern_edit_mode) {
    UpdateBlinker(selected_channel, 1);
  }

  /* Sync incremental UI states to match the full redraw results */
  last_bpm = (int)Encoder_GetValue();
  last_is_edit = is_edit_mode;
  last_is_pattern_edit = is_pattern_edit_mode;
  last_is_playing = is_playing;
  last_drawn_channel =
      (is_edit_mode || is_pattern_edit_mode) ? selected_channel : 0xFF;
}

static void UpdateModeUI(void) {
  int current_bpm = (int)Encoder_GetValue();

  /* Update Header only on mode or BPM change */
  if (is_pattern_edit_mode != last_is_pattern_edit ||
      is_edit_mode != last_is_edit ||
      (!is_edit_mode && !is_pattern_edit_mode && current_bpm != last_bpm)) {
    if (is_pattern_edit_mode) {
      ST7789_WriteString(10, 10, "PATTERN EDIT ", CYAN, BLACK, 2);
    } else if (is_edit_mode) {
      /* Overwrite with padded string */
      ST7789_WriteString(10, 10, "DRUMSET EDIT ", YELLOW, BLACK, 2);
    } else {
      char val_buf[20];
      snprintf(val_buf, sizeof(val_buf), "BPM: %d      ", current_bpm);
      ST7789_WriteString(10, 10, val_buf, WHITE, BLACK, 2);
    }
    last_is_pattern_edit = is_pattern_edit_mode;
    last_is_edit = is_edit_mode;
    last_bpm = current_bpm;

    /* Re-draw Pattern Slot in case of header overwrite */
    if (loaded_pattern_slot > 0) {
      char pat_buf[16];
      snprintf(pat_buf, sizeof(pat_buf), "P-%03d", loaded_pattern_slot);
      ST7789_WriteString(170, 10, pat_buf, YELLOW, BLACK, 2);
    }
  }

  /* Update Status Footer only on playback state change */
  if (is_playing != last_is_playing) {
    const char *status = is_playing ? "PLAYING      " : "STOPPED      ";
    uint16_t status_color = is_playing ? GREEN : RED;
    ST7789_WriteString(10, 220, status, status_color, BLACK, 2);
    last_is_playing = is_playing;
  }

  /* Handle Channel Highlight */
  if (is_edit_mode || is_pattern_edit_mode) {
    if (last_drawn_channel != selected_channel) {
      /* Unhighlight previous if it was valid */
      if (last_drawn_channel < NUM_CHANNELS) {
        UpdateBlinker(last_drawn_channel, 0);
      }
      UpdateBlinker(selected_channel, 1);
      last_drawn_channel = selected_channel;
    }
  } else {
    /* Clear any active highlight when exiting edit mode */
    if (last_drawn_channel != 0xFF) {
      for (int i = 0; i < NUM_CHANNELS; i++) {
        UpdateBlinker(i, 0);
      }
      last_drawn_channel = 0xFF;
    }
  }
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
  if (pressed && button_id == BUTTON_START) {
    is_playing = !is_playing;
    if (is_playing) {
      Sequencer_Start();
      GPIOC_ODR &= ~(1 << 13); /* ON */
    } else {
      Sequencer_Stop();
      GPIOC_ODR |= (1 << 13); /* OFF */
      needs_ui_refresh = 1;
    }
    return;
  }

  /* Handle drumset menu first */
  if (is_drumset_menu_mode) {
    if (pressed) {
      if (button_id == BUTTON_ENCODER) {
        if (is_drumset_menu_mode == 1) {
          /* Main menu selection */
          if (drumset_menu_index == 0) {
            /* LOAD Kit */
            occupied_slot_count = Drumset_GetOccupiedSlots(occupied_slots, 100);
            is_drumset_menu_mode = 3; /* Load Slot Selection */
            if (occupied_slot_count > 0) {
              Encoder_SetLimits(0, occupied_slot_count - 1);
              Encoder_SetValue(0);
              selected_slot = occupied_slots[0];
            }
            mode_changed = 1;
            full_redraw_needed = 1;
          } else if (drumset_menu_index == 1) {
            /* SAVE Kit */
            occupied_slot_count = Drumset_GetOccupiedSlots(occupied_slots, 100);
            is_drumset_menu_mode = 2; /* Save Slot Selection */
            Encoder_SetLimits(1, 100);
            Encoder_SetValue(selected_slot);
            mode_changed = 1;
            full_redraw_needed = 1;
          } else if (drumset_menu_index == 2) {
            /* BACK */
            ExitDrumsetMenu();
          }
        } else if (is_drumset_menu_mode == 2) {
          /* SAVE Action */
          if (Drumset_Save(current_drumset, selected_slot) == 0) {
            ShowPopup("DRUMSET SAVED", GREEN, 1);
          } else {
            ShowPopup("ERR SAVE", RED, 0);
          }
        } else if (is_drumset_menu_mode == 3) {
          /* LOAD Action */
          if (Drumset_LoadFromSlot(current_drumset, selected_slot) == 0) {
            ShowPopup("DRUMSET LOADED", GREEN, 1);
          } else {
            ShowPopup("ERR LOAD", RED, 0);
          }
        }
      } else if (button_id == BUTTON_DRUMSET) {
        /* Back in drumset menu handled via state change */
        if (is_drumset_menu_mode == 1) {
          ExitDrumsetMenu();
        } else {
          /* Go back to main menu */
          is_drumset_menu_mode = 1;
          drumset_menu_index = 0;
          Encoder_SetLimits(0, 2);
          Encoder_SetValue(0);
          mode_changed = 1;
          full_redraw_needed = 1;
        }
      }
    }
    return; /* Don't process other buttons while in drumset menu */
  }

  /* Handle pattern menu - New priority position */
  if (is_pattern_menu_mode) {
    if (pressed) {
      if (button_id == BUTTON_ENCODER) {
        if (is_pattern_menu_mode == 1) {
          /* Handle Pattern Menu Selection */
          if (pattern_menu_index == 0) { /* LOAD */
            is_pattern_menu_mode = 3;
            occupied_slot_count = Pattern_GetOccupiedSlots(occupied_slots, 100);
            if (occupied_slot_count > 0) {
              selected_slot = occupied_slots[0];
              Encoder_SetLimits(0, occupied_slot_count - 1);
              Encoder_SetValue(0);
            }
            full_redraw_needed = 1;
          } else if (pattern_menu_index == 1) { /* SAVE */
            is_pattern_menu_mode = 2;
            selected_slot = 1;
            Encoder_SetLimits(1, 100);
            Encoder_SetValue(1);
            occupied_slot_count = Pattern_GetOccupiedSlots(occupied_slots, 100);
            full_redraw_needed = 1;
          } else { /* BACK */
            ExitPatternMenu();
          }
          mode_changed = 1;
        } else if (is_pattern_menu_mode == 2) {
          /* SAVE current pattern */
          Pattern *pat = Sequencer_GetPattern();
          if (Pattern_Save(pat, selected_slot) == 0) {
            loaded_pattern_slot = selected_slot;
            ShowPopup("PATTERN SAVED", GREEN, 2);
          } else {
            ShowPopup("ERR SAVE", RED, 0);
          }
        } else if (is_pattern_menu_mode == 3) {
          /* LOAD selected pattern */
          if (occupied_slot_count > 0) {
            Pattern temp_pat;
            if (Pattern_Load(&temp_pat, selected_slot) == 0) {
              /* Always switch to main screen on pattern load */
              is_pattern_edit_mode = 0;
              is_pattern_detail_mode = 0;
              is_edit_mode = 0;

              if (is_playing) {
                /* Queue for next loop */
                Sequencer_QueuePattern(&temp_pat, selected_slot);
                ExitPatternMenu();
              } else {
                /* Load immediately */
                Pattern *current = Sequencer_GetPattern();
                uint16_t current_bpm = current->bpm;
                memcpy(current, &temp_pat, sizeof(Pattern));
                current->bpm = current_bpm; /* Restore current tempo */
                /* Note: BPM update intentionally disabled per user request */
                loaded_pattern_slot = selected_slot;
                ExitPatternMenu();
                ShowPopup("PATTERN LOADED", GREEN,
                          0); /* Small success pop, no exit type since already
                                 exited */
              }
            } else {
              ShowPopup("ERR LOAD", RED, 0);
            }
          }
        }
      } else if (button_id == BUTTON_PATTERN) {
        if (is_pattern_menu_mode == 1) {
          ExitPatternMenu();
        } else {
          /* Go back to main pattern menu */
          is_pattern_menu_mode = 1;
          pattern_menu_index = 0;
          Encoder_SetLimits(0, 2);
          Encoder_SetValue(0);
          mode_changed = 1;
          full_redraw_needed = 1;
        }
      }
    }
    return;
  }

  if (pressed) {
    if (button_id == BUTTON_ENCODER) {
      if (is_pattern_edit_mode) {
        if (!is_pattern_detail_mode) {
          is_pattern_detail_mode = 1;
          pattern_cursor = 0;
          Encoder_SetLimits(0, 31);
          Encoder_SetValue(0);
          Encoder_ResetIncrement();
          full_redraw_needed =
              1; /* Step grid is totally different, must clear */
          mode_changed = 1;
        } else {
          /* Cycle Step velocity */
          Sequencer_CycleStep(selected_channel, pattern_cursor);
          needs_step_update = 1; /* Trigger incremental update in main loop */
        }
        return;
      }

      if (is_channel_edit_mode == 1) {
        /* MENU SELECTION */
        if (edit_menu_index == 0) {
          /* Go to Browser */
          if (current_cluster == 0) {
            current_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "SAMPLES");
            if (current_cluster == 0) {
              current_cluster = FAT32_GetRootCluster();
              strcpy(browser_path, ""); // Root
            }
          }
          ScanDirectory();
          selected_file_index = 0;
          last_selected_file_index = -1; /* Force redraw of selected item */
          Encoder_SetLimits(0, file_count > 0 ? file_count - 1 : 0);
          Encoder_SetValue(0);
          is_channel_edit_mode = 2;
          mode_changed = 1;
          full_redraw_needed = 1;
        } else if (edit_menu_index == 1) {
          /* Go to Vol Edit */
          Encoder_SetLimits(0, 255);
          Encoder_SetValue(current_drumset->volumes[selected_channel]);
          is_channel_edit_mode = 3;
          mode_changed = 1; /* Redraw will clear selection frame */
        } else if (edit_menu_index == 2) {
          /* Go to Pan Edit */
          Encoder_SetLimits(0, 255);
          Encoder_SetValue(current_drumset->pans[selected_channel]);
          is_channel_edit_mode = 4;
          mode_changed = 1; /* Redraw will clear selection frame */
        }
      } else if (is_channel_edit_mode == 2) {
        /* Browser Action */
        if (file_count > 0) {
          FAT32_FileEntry *selected = &file_list[selected_file_index];

          if (selected->is_dir) {
            /* Enter Directory */
            current_cluster = selected->first_cluster;
            if (current_cluster == 0) {
              // ".." logic - go up
              current_cluster =
                  FAT32_GetRootCluster(); // simplified - should track parent

              // Handle path string for ".."
              if (strcmp(selected->name, "..") == 0) {
                char *last_slash = strrchr(browser_path, '/');
                if (last_slash)
                  *last_slash = '\0';
                else
                  strcpy(browser_path, "");
              } else {
                if (strlen(browser_path) > 0)
                  strcat(browser_path, "/");
                strcat(browser_path, selected->name);
              }
            } else {
              // Enter Directory
              if (strcmp(selected->name, "..") == 0) {
                // Going up logic is tricky with just clusters,
                // but for path string:
                char *last_slash = strrchr(browser_path, '/');
                if (last_slash)
                  *last_slash = '\0';
                else
                  strcpy(browser_path, "");
              } else {
                if (strlen(browser_path) > 0)
                  strcat(browser_path, "/");
                strcat(browser_path, selected->name);
              }
            }

            ScanDirectory();
            selected_file_index = 0;
            last_selected_file_index = -1;
            Encoder_SetLimits(0, file_count > 0 ? file_count - 1 : 0);
            Encoder_SetValue(0);
            mode_changed = 1;
            full_redraw_needed = 1;
          } else {
            /* Check for [EMPTY] */
            if (strcmp(selected->name, "[EMPTY]") == 0) {
              WAV_UnloadChannel(selected_channel, current_drumset);
              /* Return to Menu */
              is_channel_edit_mode = 1;
              Encoder_SetLimits(0, 2);
              Encoder_SetValue(0);
              mode_changed = 1;
              full_redraw_needed = 1;
            } else {
              /* Load Selected Sample */
              int res =
                  WAV_LoadSample(selected, selected_channel, current_drumset);
              if (res > 0) {
                /* Store full path in Drumset */
                char full_path[256];
                if (strlen(browser_path) > 0)
                  snprintf(full_path, sizeof(full_path), "%s/%s", browser_path,
                           selected->name);
                else
                  snprintf(full_path, sizeof(full_path), "%s", selected->name);

                strncpy(current_drumset->sample_paths[selected_channel],
                        full_path, 63);
                current_drumset->sample_paths[selected_channel][63] = '\0';

                /* Quick Preview */
                AudioMixer_Trigger(selected_channel, 255);
              } else {
                ShowPopup("ERR WAV", RED, 0);
              }
            }
          }
        }
      } else if (is_channel_edit_mode == 3 || is_channel_edit_mode == 4) {
        /* Confirm Vol/Pan Change -> Return to Menu */
        is_channel_edit_mode = 1;
        Encoder_SetLimits(0, 2);
        Encoder_SetValue(edit_menu_index);
        mode_changed = 1;
        last_menu_index = -1;
      } else if (is_edit_mode) {
        /* Enter Channel Edit */
        TriggerChannelEdit();
      } else {
        Encoder_ToggleIncrement();
      }
    } else if (button_id == BUTTON_DRUMSET) {
      if (is_pattern_detail_mode) {
        if (pressed) {
          is_pattern_detail_mode = 0;
          Encoder_SetLimits(0, NUM_CHANNELS - 1);
          Encoder_SetValue(selected_channel);
          full_redraw_needed = 1;
          mode_changed = 1;
          button_drumset_handled = 1;
        }
        return;
      }

      if (is_channel_edit_mode == 2) {
        /* Back from Browser -> Menu */
        is_channel_edit_mode = 1; /* Go to Menu */
        Encoder_SetLimits(0, 2);  /* 3 Menu Items */
        Encoder_SetValue(0);      /* Reset to Sample Item */
        mode_changed = 1;
        full_redraw_needed = 1;
      } else if (is_channel_edit_mode) {
        ExitChannelEdit();
      } else {
        button_drumset_pressed = 1;
        button_drumset_start_time = HAL_GetTick();
        button_drumset_handled = 0;
      }
    } else if (button_id == BUTTON_PATTERN) {
      if (pressed) {
        button_pattern_pressed = 1;
        button_pattern_start_time = HAL_GetTick();
        button_pattern_handled = 0;
      }
    }
  } else {
    /* Button released - Handled in main loop polling for BUTTON_DRUMSET */
  }
}

static uint16_t GetChannelColor(uint8_t channel) {
  switch (channel) {
  case 0:
    return RED;
  case 1:
    return GREEN;
  case 2:
    return YELLOW;
  case 3:
    return MAGENTA;
  case 4:
    return CYAN;
  case 5:
    return ORANGE;
  default:
    return WHITE;
  }
}

static void ShowPopup(const char *msg, uint16_t color, uint8_t exit_type) {
  ST7789_FillRect(50, 100, 220, 40, BLACK);
  ST7789_DrawThickFrame(50, 100, 220, 40, 2, WHITE);
  ST7789_WriteString(80, 112, msg, color, BLACK, 2);
  is_ui_popup = 1;
  ui_popup_start_time = HAL_GetTick();
  ui_popup_exit_type = exit_type;
}

static void DrawStepEditScreen(uint8_t full_redraw) {
  const int BOX_W = 34;
  const int BOX_H = 36; /* Taller boxes for full screen */
  const int GAP_X = 4;
  const int GAP_Y = 6;
  const int START_X = 12;
  const int START_Y = 50;

  static int last_cursor = -1;
  static int last_play_step = -1;

  uint16_t ch_color = GetChannelColor(selected_channel);
  uint16_t bg_box_color = 0x2104;
  uint8_t current_play_step = is_playing ? Sequencer_GetCurrentStep() : 0xFF;

  if (full_redraw) {
    ST7789_Fill(BLACK);

    /* Dedicated Header */
    char tit_buf[48];
    snprintf(tit_buf, sizeof(tit_buf), "STEP EDIT: CH %d",
             selected_channel + 1);
    ST7789_WriteString(10, 10, tit_buf, CYAN, BLACK, 2);

    /* Sample Name below title */
    ST7789_WriteString(10, 32, current_drumset->sample_names[selected_channel],
                       ch_color, BLACK, 1);

    last_cursor = -1;
    last_play_step = -1;
  } else {
    /* Incremental update guard: Don't draw if menu is active */
    /* Incremental update guard: Don't draw if menu is active */
    if (is_pattern_menu_mode || is_drumset_menu_mode || full_redraw_needed)
      return;

    if (full_redraw == 2) {
      /* Mode 2: Full grid refresh without ST7789_Fill(BLACK) */
      /* We just reset last_cursor/last_play_step to force redraw of all */
      last_cursor = -1;
      last_play_step = -1;
    }
  }

  /* Draw 32 steps (4x8 grid) */
  for (int i = 0; i < 32; i++) {
    int row = i / 8;
    int col = i % 8;
    int x = START_X + col * (BOX_W + GAP_X);
    int y = START_Y + row * (BOX_H + GAP_Y);

    /* Optimized incremental redraw condition to prevent flicker */
    uint8_t redraw = (full_redraw == 1 || full_redraw == 2);
    if (i == current_play_step || i == last_play_step)
      redraw = 1;
    if (i == pattern_cursor || i == last_cursor) {
      /* Redraw cursor only if it moved or if we intentionally triggered an
       * update (e.g. velocity change) */
      if (pattern_cursor != last_cursor || full_redraw == 3)
        redraw = 1;
    }

    if (redraw) {

      uint8_t velocity = Sequencer_GetStep(selected_channel, i);

      /* 1. Clear Box with Background Color */
      ST7789_FillRect(x, y, BOX_W, BOX_H, bg_box_color);

      /* 2. Draw Velocity-based Indicator */
      if (velocity > 0) {
        if (velocity >= 255) {
          ST7789_FillRect(x, y, BOX_W, BOX_H, ch_color);
        } else if (velocity >= 128) {
          ST7789_FillRect(x + 5, y + 6, 24, 24, ch_color);
        } else if (velocity >= 64) {
          ST7789_FillRect(x + 9, y + 10, 16, 16, ch_color);
        } else {
          ST7789_FillRect(x + 13, y + 14, 8, 8, ch_color);
        }
      }

      /* 3. Draw Manual Selection Frame (White) */
      if (i == pattern_cursor) {
        ST7789_DrawThickFrame(x, y, BOX_W, BOX_H, 2, WHITE);
      }

      /* 4. Draw Playhead Indicator (Centered 10x10 White Square) if active */
      if (i == current_play_step) {
        ST7789_FillRect(x + (BOX_W / 2) - 5, y + (BOX_H / 2) - 5, 10, 10,
                        WHITE);
      }
    }
  }

  last_cursor = pattern_cursor;
  last_play_step = current_play_step;
}
