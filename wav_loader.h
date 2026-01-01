#ifndef WAV_LOADER_H
#define WAV_LOADER_H

#include <stdint.h>

#define NUM_CHANNELS 4

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
 * @brief Load WAV file from SD card
 * @param path File path
 * @param data Pointer to store sample data pointer
 * @param length Pointer to store sample length (in samples)
 * @return 0 on success, -1 on error
 */
int WAV_Load(const char *path, int16_t **data, uint32_t *length);

/**
 * @brief Load drumset from SD card
 * @param kit_path Path to drumset folder
 * @param drumset Pointer to drumset structure
 * @return 0 on success, -1 on error
 */
int Drumset_Load(const char *kit_path, Drumset *drumset);

/**
 * @brief Free drumset memory
 * @param drumset Pointer to drumset structure
 */
void Drumset_Free(Drumset *drumset);

#endif
