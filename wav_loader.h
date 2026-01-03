#ifndef WAV_LOADER_H
#define WAV_LOADER_H

#include "fat32.h"
#include <stdint.h>

#define NUM_CHANNELS 6

/**
 * @brief Drumset structure
 */
typedef struct {
  char name[32];
  int16_t *samples[NUM_CHANNELS];
  uint32_t lengths[NUM_CHANNELS];
  uint8_t volumes[NUM_CHANNELS];
  uint8_t pans[NUM_CHANNELS];
  char sample_names[NUM_CHANNELS][16];
} Drumset;

/**
 * @brief Load drumset from SD card
 * @param kit_path Path to drumset folder (unused, reserved for future)
 * @param drumset Pointer to drumset structure
 * @return 0 on success, -1 on error
 */
int Drumset_Load(const char *kit_path, Drumset *drumset);

/**
 * @brief Load a single WAV file into a specific channel
 * @param file_entry FAT32 file entry of the WAV file
 * @param channel_idx Index of the channel to load into (0-5)
 * @param drumset Pointer to Drumset structure to update
 * @return Number of samples loaded, or negative on error
 */
int WAV_LoadSample(FAT32_FileEntry *file_entry, uint8_t channel_idx,
                   Drumset *drumset);

#endif
