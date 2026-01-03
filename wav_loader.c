#include "wav_loader.h"
#include "audio_mixer.h"
#include "fat32.h"
#include "sdcard.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

    /* Note: sample_paths is set by the caller (main.c browser) or LoadFromSlot
     */

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
  drumset->sample_paths[channel][0] = '\0';
}

int Drumset_Save(Drumset *drumset, uint8_t slot) {
  if (slot < 1 || slot > 100) {
    return -1;
  }

  // Find or create DRUMSETS directory
  uint32_t drumsets_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "DRUMSETS");
  if (drumsets_cluster == 0) {
    // DRUMSETS folder doesn't exist - for now, return error
    // TODO: Create directory if needed
    return -1;
  }

  // Generate filename: KIT-XXX.DRM
  char filename[13];
  snprintf(filename, sizeof(filename), "KIT-%03d.DRM", slot);

  // Serialize drumset to text buffer
  char buffer[512];
  int offset = 0;

  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    // Format: channel,sample_path,volume,pan\n
    // For sample path, use relative path from SAMPLES folder
    const char *sample_name = drumset->sample_names[ch];

    // Build sample path
    // Build sample path
    char sample_path[64];
    if (strcmp(sample_name, "EMPTY") == 0) {
      strcpy(sample_path, "EMPTY");
    } else {
      // Use stored full path if available, otherwise fallback to default
      // SAMPLES/
      if (strlen(drumset->sample_paths[ch]) > 0) {
        strncpy(sample_path, drumset->sample_paths[ch], sizeof(sample_path));
        sample_path[sizeof(sample_path) - 1] = '\0';
      } else {
        snprintf(sample_path, sizeof(sample_path), "SAMPLES/%s.WAV",
                 sample_name);
      }
    }

    int written =
        snprintf(buffer + offset, sizeof(buffer) - offset, "%d,%s,%d,%d\n", ch,
                 sample_path, drumset->volumes[ch], drumset->pans[ch]);

    if (written < 0 || (offset + (uint32_t)written) >= sizeof(buffer)) {
      return -1; // Buffer overflow
    }

    offset += written;
  }

  // Write to SD card
  return FAT32_WriteFile(drumsets_cluster, filename, (uint8_t *)buffer, offset);
}

int Drumset_LoadFromSlot(Drumset *drumset, uint8_t slot) {
  if (slot < 1 || slot > 100) {
    return -1;
  }

  // Find DRUMSETS directory
  uint32_t drumsets_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "DRUMSETS");
  if (drumsets_cluster == 0) {
    return -1;
  }

  // Generate filename
  char filename[13];
  snprintf(filename, sizeof(filename), "KIT-%03d.DRM", slot);

  // Check if file exists
  if (!FAT32_FileExists(drumsets_cluster, filename)) {
    return -1;
  }

  // Find the file entry to read it
  FAT32_FileEntry files[FAT32_MAX_FILES];
  int count = FAT32_ListDir(drumsets_cluster, files, FAT32_MAX_FILES);

  FAT32_FileEntry *target_file = NULL;
  for (int i = 0; i < count; i++) {
    if (strcasecmp(files[i].name, filename) == 0) {
      target_file = &files[i];
      break;
    }
  }

  if (!target_file) {
    return -1;
  }

  // Read file content
  uint32_t sector = FAT32_GetFileSector(target_file);
  uint8_t buffer[512];

  if (SDCARD_ReadBlock(sector, buffer) != SDCARD_OK) {
    return -1;
  }

  // Null-terminate to prevent parsing junk in the rest of the sector
  if (target_file->size < 512) {
    buffer[target_file->size] = '\0';
  }

  // Parse text format
  char *line = (char *)buffer;
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    int channel_num;
    char sample_path[64];
    int volume, pan;

    // Parse line: channel,sample_path,volume,pan
    int parsed = sscanf(line, "%d,%63[^,],%d,%d", &channel_num, sample_path,
                        &volume, &pan);

    if (parsed != 4 || channel_num != ch) {
      // Allow partial reads or end of file? If error, maybe stop or continue?
      // For robustness, try next line? But format is strict.
      // If parsing failed, maybe buffer ended?
      break;
    }

    // Set volume and pan
    drumset->volumes[ch] = volume;
    drumset->pans[ch] = pan;

    // Apply to AudioMixer
    AudioMixer_SetVolume(ch, volume);
    AudioMixer_SetPan(ch, pan);

    // Load sample
    if (strcmp(sample_path, "EMPTY") == 0) {
      WAV_UnloadChannel(ch, drumset);
    } else {
      /* Store path */
      strncpy(drumset->sample_paths[ch], sample_path, 63);
      drumset->sample_paths[ch][63] = '\0';

      int loaded = 0;

      /* Check if path contains '/' */
      const char *last_slash = strrchr(sample_path, '/');
      if (last_slash) {
        // Path traversal logic
        uint32_t curr_clus = FAT32_GetRootCluster();

        char path_copy[64];
        strncpy(path_copy, sample_path, 63);
        path_copy[63] = '\0';

        char *token = strtok(path_copy, "/");
        int found_path = 1;
        char file_name[16] = {0};

        while (token != NULL) {
          if (strchr(token, '.') != NULL) {
            // This is the file
            strncpy(file_name, token, 15);
            break;
          }

          // This is a directory
          curr_clus = FAT32_FindDir(curr_clus, token);
          if (curr_clus == 0) {
            found_path = 0;
            break;
          }
          token = strtok(NULL, "/");
        }

        if (found_path && strlen(file_name) > 0) {
          static FAT32_FileEntry sample_file;
          FAT32_FileEntry dir_files[FAT32_MAX_FILES];
          int cnt = FAT32_ListDir(curr_clus, dir_files, FAT32_MAX_FILES);
          int found_file = 0;
          for (int f = 0; f < cnt; f++) {
            if (strcasecmp(dir_files[f].name, file_name) == 0) {
              sample_file = dir_files[f];
              found_file = 1;
              break;
            }
          }

          if (found_file) {
            if (WAV_LoadSample(&sample_file, ch, drumset) > 0) {
              loaded = 1;
            }
          }
        }

      } else {
        // Fallback to SAMPLES/ lookup
        uint32_t samples_clus =
            FAT32_FindDir(FAT32_GetRootCluster(), "SAMPLES");
        if (samples_clus) {
          FAT32_FileEntry dir_files[FAT32_MAX_FILES];
          int cnt = FAT32_ListDir(samples_clus, dir_files, FAT32_MAX_FILES);
          for (int f = 0; f < cnt; f++) {
            if (strcasecmp(dir_files[f].name, sample_path) == 0) {
              if (WAV_LoadSample(&dir_files[f], ch, drumset) > 0) {
                loaded = 1;
              }
              break;
            }
          }
        }
      }

      if (!loaded) {
        // Legacy Fallback: Try extracting filename and looking in SAMPLES/
        // Ensure we don't try if we already tried SAMPLES/ above
        const char *fname = strrchr(sample_path, '/');
        if (fname)
          fname++;
        else
          fname = sample_path;

        uint32_t samples_clus =
            FAT32_FindDir(FAT32_GetRootCluster(), "SAMPLES");
        if (samples_clus) {
          static FAT32_FileEntry dir_files[FAT32_MAX_FILES];
          int cnt = FAT32_ListDir(samples_clus, dir_files, FAT32_MAX_FILES);
          for (int f = 0; f < cnt; f++) {
            if (strcasecmp(dir_files[f].name, fname) == 0) {
              if (WAV_LoadSample(&dir_files[f], ch, drumset) > 0) {
                loaded = 1;
              }
              break;
            }
          }
        }
      }

      if (!loaded) {
        WAV_UnloadChannel(ch, drumset);
        drumset->volumes[ch] = 255;
        drumset->pans[ch] = 128;
      }
    }

    // Move to next line
    line = strchr(line, '\n');
    if (line) {
      line++; // Skip newline
    } else {
      break;
    }
  }

  // Set Kit Name
  snprintf(drumset->name, sizeof(drumset->name), "KIT-%03d", slot);

  return 0;
}

int Drumset_GetOccupiedSlots(uint8_t *slots, int max_slots) {
  // Find DRUMSETS directory
  uint32_t drumsets_cluster = FAT32_FindDir(FAT32_GetRootCluster(), "DRUMSETS");
  if (drumsets_cluster == 0) {
    return 0; // No DRUMSETS folder
  }

  FAT32_FileEntry files[FAT32_MAX_FILES];
  int count = FAT32_ListDir(drumsets_cluster, files, FAT32_MAX_FILES);

  int occupied_count = 0;

  for (int i = 0; i < count && occupied_count < max_slots; i++) {
    // Check if filename matches KIT-XXX.DRM pattern
    if (strncmp(files[i].name, "KIT-", 4) == 0) {
      // Extract slot number
      int slot_num;
      if (sscanf(files[i].name + 4, "%d", &slot_num) == 1) {
        if (slot_num >= 1 && slot_num <= 100) {
          slots[occupied_count++] = slot_num;
        }
      }
    }
  }

  return occupied_count;
}
