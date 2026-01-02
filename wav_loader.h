#ifndef WAV_LOADER_H
#define WAV_LOADER_H

#include <stdint.h>

#define NUM_CHANNELS 6

/**
 * @brief Drumset structure
 */
typedef struct {
  char name[32];
  int16_t *samples[NUM_CHANNELS];
  uint32_t lengths[NUM_CHANNELS];
  char sample_names[NUM_CHANNELS][16];
} Drumset;

/**
 * @brief Load drumset from SD card
 * @param kit_path Path to drumset folder (unused, reserved for future)
 * @param drumset Pointer to drumset structure
 * @return 0 on success, -1 on error
 */
int Drumset_Load(const char *kit_path, Drumset *drumset);

#endif
