#include "wav_loader.h"
#include "audio_mixer.h"
#include "fat32.h"
#include "sdcard.h"
#include <stdio.h>
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

/* Static sample buffers (6 channels x 16KB each = 96KB) */
#define MAX_SAMPLE_SIZE (16 * 1024 / 2) // 16KB = 8192 samples
static int16_t sample_buffers[NUM_CHANNELS][MAX_SAMPLE_SIZE];

/**
 * @brief Load WAV file into provided buffer
 * @param file_entry File entry to load
 * @param buffer Buffer to load into
 * @param max_samples Maximum samples to load
 * @return Number of samples loaded, or -1 on error
 */
static int load_wav_to_buffer(FAT32_FileEntry *file_entry, int16_t *buffer,
                              uint32_t max_samples) {
  /* Get first sector */
  uint32_t sector = FAT32_GetFileSector(file_entry);
  if (sector == 0) {
    return -1;
  }

  // Read first sector for header
  if (SDCARD_ReadBlock(sector, sector_buffer) != SDCARD_OK) {
    return -1;
  }

  // Parse header
  WAVHeader *header = (WAVHeader *)sector_buffer;

  // Validate RIFF/WAVE header
  if (memcmp(header->riff, "RIFF", 4) != 0 ||
      memcmp(header->wave, "WAVE", 4) != 0) {
    return -2; // Bad header
  }

  // Check PCM format
  if (header->audio_format != 1) {
    return -3; // Not PCM
  }

  // Check sample rate
  if (header->sample_rate != 44100) {
    return -4; // Wrong sample rate
  }

  // Check bit depth
  if (header->bits_per_sample != 16) {
    return -5; // Wrong bit depth
  }

  // Check mono (we only support mono for now, stereo mixing can be added later)
  if (header->num_channels != 1) {
    return -6; // Not mono
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

  /* Scan root or SAMPLES directory */
  uint32_t scan_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "SAMPLES");
  if (scan_cluster == 0) {
    scan_cluster = FAT32_GetRootCluster();
  }

  FAT32_FileEntry dir_files[FAT32_MAX_FILES];
  int file_count = FAT32_ListDir(scan_cluster, dir_files, FAT32_MAX_FILES);
  if (file_count < 0)
    file_count = 0;

  if (file_count < 0)
    file_count = 0;

  /* Initialize defaults */
  for (int c = 0; c < NUM_CHANNELS; c++) {
    drumset->volumes[c] = 255;
    drumset->pans[c] = 128;
  }

  int channel = 0;

  /* Load first 4 WAV files found */
  for (int i = 0; i < file_count && channel < NUM_CHANNELS; i++) {
    char *fname = dir_files[i].name;
    int len = strlen(fname);

    /* Check for .WAV extension (case insensitive) */
    if (len > 4) {
      const char *ext = &fname[len - 4];
      if ((ext[0] == '.' && (ext[1] == 'W' || ext[1] == 'w') &&
           (ext[2] == 'A' || ext[2] == 'a') &&
           (ext[3] == 'V' || ext[3] == 'v'))) {

        /* Load this file */
        drumset->samples[channel] = sample_buffers[channel];
        int samples_loaded = load_wav_to_buffer(
            &dir_files[i], sample_buffers[channel], MAX_SAMPLE_SIZE);

        if (samples_loaded > 0) {
          drumset->lengths[channel] = samples_loaded;

          /* Use filename (without .wav) as label */
          strncpy(drumset->sample_names[channel], fname,
                  sizeof(drumset->sample_names[channel]) - 1);
          drumset->sample_names[channel]
                               [sizeof(drumset->sample_names[channel]) - 1] =
              '\0';

          /* Remove .wav extension */
          char *dot = strchr(drumset->sample_names[channel], '.');
          if (dot)
            *dot = '\0';

          channel++;
        } else {
          /* Show error for failed loads */
          if (channel == 0) {
            char err_msg[16];
            snprintf(err_msg, sizeof(err_msg), "ERR:%d", samples_loaded);
            strncpy(drumset->sample_names[channel], err_msg,
                    sizeof(drumset->sample_names[channel]) - 1);
          } else {
            strncpy(drumset->sample_names[channel], "LOAD ERR",
                    sizeof(drumset->sample_names[channel]) - 1);
          }
          drumset->sample_names[channel]
                               [sizeof(drumset->sample_names[channel]) - 1] =
              '\0';
          drumset->samples[channel] = sample_buffers[channel];
          drumset->lengths[channel] = 1000;
          for (uint32_t j = 0; j < 1000; j++)
            sample_buffers[channel][j] = 0;
          channel++;
        }
      }
    }
  }

  /* Fill remaining channels with EMPTY */
  for (; channel < NUM_CHANNELS; channel++) {
    strncpy(drumset->sample_names[channel], "EMPTY",
            sizeof(drumset->sample_names[channel]) - 1);
    drumset->sample_names[channel][sizeof(drumset->sample_names[channel]) - 1] =
        '\0';
    drumset->samples[channel] = sample_buffers[channel];
    drumset->lengths[channel] = 1000;
    for (uint32_t j = 0; j < 1000; j++)
      sample_buffers[channel][j] = 0;
  }

  (void)kit_path;
  return 0;
}

int WAV_LoadSample(FAT32_FileEntry *file_entry, uint8_t channel_idx,
                   Drumset *drumset) {
  if (channel_idx >= NUM_CHANNELS) {
    return -1;
  }

  /* Load this file */
  drumset->samples[channel_idx] = sample_buffers[channel_idx];
  int samples_loaded = load_wav_to_buffer(
      file_entry, sample_buffers[channel_idx], MAX_SAMPLE_SIZE);

  if (samples_loaded > 0) {
    drumset->lengths[channel_idx] = samples_loaded;

    /* Use filename (without .wav) as label */
    strncpy(drumset->sample_names[channel_idx], file_entry->name,
            sizeof(drumset->sample_names[channel_idx]) - 1);
    drumset->sample_names[channel_idx]
                         [sizeof(drumset->sample_names[channel_idx]) - 1] =
        '\0';

    /* Remove .wav extension */
    char *dot = strchr(drumset->sample_names[channel_idx], '.');
    if (dot)
      *dot = '\0';

    /* Update AudioMixer with new sample */
    AudioMixer_SetSample(channel_idx, sample_buffers[channel_idx],
                         samples_loaded);
  } else {
    /* Clear channel on error */
    drumset->lengths[channel_idx] = 1000;
    for (uint32_t j = 0; j < 1000; j++) {
      sample_buffers[channel_idx][j] = 0;
    }
  }

  return samples_loaded;
}

void WAV_UnloadChannel(uint8_t channel, Drumset *drumset) {
  if (channel >= NUM_CHANNELS)
    return;

  /* Clear sample buffer */
  drumset->lengths[channel] = 0;
  for (uint32_t i = 0; i < 1000; i++) {
    sample_buffers[channel][i] = 0;
  }

  /* Update audio mixer to stop playing this channel */
  drumset->samples[channel] = sample_buffers[channel];
  AudioMixer_SetSample(channel, sample_buffers[channel], 0);

  /* Set name to EMPTY */
  strncpy(drumset->sample_names[channel], "EMPTY",
          sizeof(drumset->sample_names[channel]) - 1);
  drumset->sample_names[channel][sizeof(drumset->sample_names[channel]) - 1] =
      '\0';
}
