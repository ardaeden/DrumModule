#include "audio_mixer.h"
#include <string.h>

/* Audio channel structure */
typedef struct {
  int16_t *sample_data;
  uint32_t sample_length;
  uint32_t playback_pos;
  uint8_t volume;
  uint8_t active;
} AudioChannel;

/* Channel array */
static AudioChannel channels[NUM_CHANNELS];

void AudioMixer_Init(void) { memset(channels, 0, sizeof(channels)); }

void AudioMixer_SetSample(uint8_t channel, int16_t *sample_data,
                          uint32_t sample_length) {
  if (channel >= NUM_CHANNELS)
    return;

  channels[channel].sample_data = sample_data;
  channels[channel].sample_length = sample_length;
  channels[channel].playback_pos = 0;
  channels[channel].active = 0;
}

void AudioMixer_Trigger(uint8_t channel, uint8_t velocity) {
  if (channel >= NUM_CHANNELS)
    return;
  if (channels[channel].sample_data == NULL)
    return;

  channels[channel].playback_pos = 0;
  channels[channel].volume = velocity;
  channels[channel].active = 1;
}

void AudioMixer_Stop(uint8_t channel) {
  if (channel >= NUM_CHANNELS)
    return;
  channels[channel].active = 0;
}

void AudioMixer_StopAll(void) {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    channels[i].active = 0;
  }
}

void AudioMixer_Process(int16_t *output, uint32_t length) {
  for (uint32_t i = 0; i < length; i++) {
    int32_t mix_left = 0;
    int32_t mix_right = 0;

    /* Mix all active channels */
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      if (channels[ch].active &&
          channels[ch].playback_pos < channels[ch].sample_length) {

        /* Get sample */
        int32_t sample = channels[ch].sample_data[channels[ch].playback_pos++];

        /* Apply volume (velocity) */
        sample = (sample * channels[ch].volume) >> 8;

        /* Mix to both channels (mono to stereo) */
        mix_left += sample;
        mix_right += sample;

        /* Check if sample finished */
        if (channels[ch].playback_pos >= channels[ch].sample_length) {
          channels[ch].active = 0;
        }
      }
    }

    /* Clamp to 16-bit range using saturation */
    if (mix_left > 32767)
      mix_left = 32767;
    if (mix_left < -32768)
      mix_left = -32768;
    if (mix_right > 32767)
      mix_right = 32767;
    if (mix_right < -32768)
      mix_right = -32768;

    /* Output stereo interleaved */
    output[i * 2] = (int16_t)mix_left;
    output[i * 2 + 1] = (int16_t)mix_right;
  }
}
