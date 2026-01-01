#include "wav_loader.h"
#include "fat32.h"
#include "sdcard.h"
#include <stdlib.h>
#include <string.h>

/* WAV header structure */
typedef struct {
  char riff[4]; // "RIFF"
  uint32_t file_size;
  char wave[4]; // "WAVE"
  char fmt[4];  // "fmt "
  uint32_t fmt_size;
  uint16_t audio_format; // 1 = PCM
  uint16_t num_channels; // 1 = mono, 2 = stereo
  uint32_t sample_rate;  // 44100
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample; // 16
  char data[4];             // "data"
  uint32_t data_size;
} __attribute__((packed)) WAVHeader;

/* Static buffer for reading sectors */
static uint8_t sector_buffer[512];

/* Static sample buffers (4 channels × 25KB each = 100KB) */
#define MAX_SAMPLE_SIZE (25 * 1024 / 2) // 25KB = 12500 samples
static int16_t sample_buffers[NUM_CHANNELS][MAX_SAMPLE_SIZE];

/**
 * @brief Read 16-bit little-endian value
 */
static uint16_t read_u16(uint8_t *buf, uint16_t offset) {
  return buf[offset] | (buf[offset + 1] << 8);
}

/**
 * @brief Read 32-bit little-endian value
 */
static uint32_t read_u32(uint8_t *buf, uint16_t offset) {
  return buf[offset] | (buf[offset + 1] << 8) | (buf[offset + 2] << 16) |
         (buf[offset + 3] << 24);
}

/**
 * @brief Find file in root directory
 * @param filename File to find
 * @param file_entry Output file entry
 * @return 0 on success, -1 if not found
 */
static int find_file(const char *filename, FAT32_FileEntry *file_entry) {
  FAT32_FileEntry files[FAT32_MAX_FILES];
  int count = FAT32_ListRootFiles(files, FAT32_MAX_FILES);

  if (count <= 0)
    return -1;

  for (int i = 0; i < count; i++) {
    if (strcmp(files[i].name, filename) == 0) {
      *file_entry = files[i];
      return 0;
    }
  }

  return -1;
}

int WAV_Load(const char *path, int16_t **data, uint32_t *length) {
  FAT32_FileEntry file;

  // Find file
  if (find_file(path, &file) != 0) {
    return -1; // File not found
  }

  // Read first sector to get WAV header
  uint32_t first_sector = FAT32_GetFileSector(&file);
  if (SDCARD_ReadBlock(first_sector, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  // Parse WAV header
  WAVHeader *header = (WAVHeader *)sector_buffer;

  // Validate WAV format
  if (memcmp(header->riff, "RIFF", 4) != 0 ||
      memcmp(header->wave, "WAVE", 4) != 0 ||
      header->audio_format != 1 || // PCM
      header->num_channels != 1 || // Mono
      header->sample_rate != 44100 || header->bits_per_sample != 16) {
    return -1; // Invalid format
  }

  // Calculate number of samples
  uint32_t num_samples = header->data_size / 2; // 16-bit samples

  // Limit to buffer size
  if (num_samples > MAX_SAMPLE_SIZE) {
    num_samples = MAX_SAMPLE_SIZE;
  }

  *length = num_samples;

  // Note: For now, return NULL as we need to implement
  // multi-sector reading. This is a placeholder.
  *data = NULL;

  return 0;
}

/**
 * @brief Load WAV file into provided buffer
 * @param path File path
 * @param buffer Buffer to load into
 * @param max_samples Maximum samples to load
 * @return Number of samples loaded, or -1 on error
 */
static int load_wav_to_buffer(const char *path, int16_t *buffer,
                              uint32_t max_samples) {
  FAT32_FileEntry file;

  // Find file
  if (find_file(path, &file) != 0) {
    return -1;
  }

  // Get first sector
  uint32_t sector = FAT32_GetFileSector(&file);
  if (sector == 0) {
    return -1;
  }

  // Read first sector for header
  if (SDCARD_ReadBlock(sector, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  // Parse header
  WAVHeader *header = (WAVHeader *)sector_buffer;

  // Validate
  if (memcmp(header->riff, "RIFF", 4) != 0 ||
      memcmp(header->wave, "WAVE", 4) != 0 || header->audio_format != 1 ||
      header->num_channels != 1 || header->sample_rate != 44100 ||
      header->bits_per_sample != 16) {
    return -1;
  }

  // Calculate samples
  uint32_t num_samples = header->data_size / 2;
  if (num_samples > max_samples) {
    num_samples = max_samples;
  }

  // Copy first sector's data (after 44-byte header)
  uint32_t samples_copied = 0;
  uint32_t bytes_in_first_sector = 512 - 44;
  uint32_t samples_in_first_sector = bytes_in_first_sector / 2;

  if (samples_in_first_sector > num_samples) {
    samples_in_first_sector = num_samples;
  }

  // Copy from first sector
  int16_t *src = (int16_t *)(sector_buffer + 44);
  for (uint32_t i = 0; i < samples_in_first_sector; i++) {
    buffer[samples_copied++] = src[i];
  }

  // Read remaining sectors if needed
  sector++;
  while (samples_copied < num_samples) {
    if (SDCARD_ReadBlock(sector, sector_buffer) != SDCARD_OK) {
      break;
    }

    uint32_t samples_in_sector = 512 / 2;
    uint32_t samples_to_copy = num_samples - samples_copied;
    if (samples_to_copy > samples_in_sector) {
      samples_to_copy = samples_in_sector;
    }

    src = (int16_t *)sector_buffer;
    for (uint32_t i = 0; i < samples_to_copy; i++) {
      buffer[samples_copied++] = src[i];
    }

    sector++;
  }

  return samples_copied;
}

int Drumset_Load(const char *kit_path, Drumset *drumset) {
  // Try to load standard filenames from root directory
  strncpy(drumset->name, "DEFAULT KIT", sizeof(drumset->name));

  const char *filenames[] = {"KICK.WAV", "SNARE.WAV", "HATS.WAV", "CLAP.WAV"};
  const char *names[] = {"KICK", "SNARE", "HATS", "CLAP"};

  for (int i = 0; i < NUM_CHANNELS; i++) {
    strncpy(drumset->sample_names[i], names[i],
            sizeof(drumset->sample_names[i]));
    drumset->samples[i] = sample_buffers[i];

    // Try to load WAV file
    int samples_loaded =
        load_wav_to_buffer(filenames[i], sample_buffers[i], MAX_SAMPLE_SIZE);

    if (samples_loaded > 0) {
      drumset->lengths[i] = samples_loaded;
    } else {
      // File not found or error - use silence
      drumset->lengths[i] = 1000;
      for (uint32_t j = 0; j < drumset->lengths[i]; j++) {
        sample_buffers[i][j] = 0;
      }
    }
  }

  (void)kit_path;
  return 0;
}

void Drumset_Free(Drumset *drumset) {
  // Using static buffers, so nothing to free
  for (int i = 0; i < NUM_CHANNELS; i++) {
    drumset->samples[i] = NULL;
    drumset->lengths[i] = 0;
  }
}
