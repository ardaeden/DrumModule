#include "wav_loader.h"
#include "fat32.h"
#include "sdcard.h"
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
  strncpy(drumset->name, "ROOT KIT", sizeof(drumset->name));

  /* Search keywords for dynamic discovery */
  const char *keywords[] = {"KICK", "SNARE", "HATS", "CLAP"};

  /* Scan root directory once */
  FAT32_FileEntry dir_files[FAT32_MAX_FILES];
  int file_count = FAT32_ListRootFiles(dir_files, FAT32_MAX_FILES);
  if (file_count < 0)
    file_count = 0;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    char found_name[16] = "";
    int file_idx = -1;

    /* Search for keyword match (Case-Insensitive) */
    for (int j = 0; j < file_count; j++) {
      int match = 1;
      for (int k = 0; k < (int)strlen(keywords[i]); k++) {
        char c1 = dir_files[j].name[k];
        char c2 = keywords[i][k];
        if (c1 >= 'a' && c1 <= 'z')
          c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z')
          c2 -= 32;
        if (c1 != c2) {
          match = 0;
          break;
        }
      }
      if (match) {
        file_idx = j;
        strncpy(found_name, dir_files[j].name, sizeof(found_name));
        break;
      }
    }

    if (file_idx != -1) {
      /* File found - try to load it */
      drumset->samples[i] = sample_buffers[i];
      int samples_loaded =
          load_wav_to_buffer(found_name, sample_buffers[i], MAX_SAMPLE_SIZE);

      if (samples_loaded > 0) {
        /* File loaded successfully - extract label from actual filename */
        drumset->lengths[i] = samples_loaded;
        strncpy(drumset->sample_names[i], found_name,
                sizeof(drumset->sample_names[i]) - 1);
        drumset->sample_names[i][sizeof(drumset->sample_names[i]) - 1] = '\0';

        char *dot = strchr(drumset->sample_names[i], '.');
        if (dot)
          *dot = '\0';
      } else {
        /* File found but load failed - use keyword fallback */
        drumset->lengths[i] = 1000;
        strncpy(drumset->sample_names[i], keywords[i],
                sizeof(drumset->sample_names[i]) - 1);
        drumset->sample_names[i][sizeof(drumset->sample_names[i]) - 1] = '\0';
        for (uint32_t j = 0; j < 1000; j++)
          sample_buffers[i][j] = 0;
      }
    } else {
      /* File not found - use keyword fallback */
      strncpy(drumset->sample_names[i], keywords[i],
              sizeof(drumset->sample_names[i]) - 1);
      drumset->sample_names[i][sizeof(drumset->sample_names[i]) - 1] = '\0';
      drumset->samples[i] = sample_buffers[i];
      drumset->lengths[i] = 1000;
      for (uint32_t j = 0; j < 1000; j++)
        sample_buffers[i][j] = 0;
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
