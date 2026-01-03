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

/**
 * @brief Unload a channel's sample
 * @param channel Channel number (0-5)
 * @param drumset Drumset structure
 */
void WAV_UnloadChannel(uint8_t channel, Drumset *drumset);

/**
 * @brief Save drumset to a slot (1-100)
 * @param drumset Drumset to save
 * @param slot Slot number (1-100)
 * @return 0 on success, -1 on error
 */
int Drumset_Save(Drumset *drumset, uint8_t slot);

/**
 * @brief Load drumset from a slot
 * @param drumset Drumset structure to load into
 * @param slot Slot number (1-100)
 * @return 0 on success, -1 on error
 */
int Drumset_LoadFromSlot(Drumset *drumset, uint8_t slot);

/**
 * @brief Get list of occupied slots
 * @param slots Array to store occupied slot numbers
 * @param max_slots Maximum number of slots to return
 * @return Number of occupied slots found
 */
int Drumset_GetOccupiedSlots(uint8_t *slots, int max_slots);

#endif
