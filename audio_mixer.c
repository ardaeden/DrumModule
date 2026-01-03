#include "audio_mixer.h"
#include <string.h>

/* Audio channel structure */
typedef struct {
  int16_t *sample_data;
  uint32_t sample_length;
  uint32_t playback_pos;
  uint8_t volume;  /* Current trigger velocity */
  uint8_t mix_vol; /* Channel Mix Volume (0-255) */
  uint8_t pan;     /* 0 = Left, 128 = Center, 255 = Right */
  uint8_t active;
} AudioChannel;

/* Channel array */
static AudioChannel channels[NUM_CHANNELS];

void AudioMixer_Init(void) {
  memset(channels, 0, sizeof(channels));
  for (int i = 0; i < NUM_CHANNELS; i++) {
    channels[i].pan = 128;     /* Default center */
    channels[i].mix_vol = 255; /* Default max volume */
  }
}

void AudioMixer_SetSample(uint8_t channel, int16_t *sample_data,
                          uint32_t sample_length) {
  if (channel >= NUM_CHANNELS)
    return;

  channels[channel].sample_data = sample_data;
  channels[channel].sample_length = sample_length;
  channels[channel].playback_pos = 0;
  channels[channel].active = 0;
  // Preserve pan if already set, otherwise default to center if init cleared it
  if (channels[channel].pan == 0 && channels[0].pan == 0)
    channels[channel].pan = 128;
}

void AudioMixer_SetPan(uint8_t channel, uint8_t pan) {
  if (channel >= NUM_CHANNELS)
    return;
  channels[channel].pan = pan;
}

void AudioMixer_SetVolume(uint8_t channel, uint8_t volume) {
  if (channel >= NUM_CHANNELS)
    return;
  channels[channel].mix_vol = volume;
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

        /* Apply mix volume */
        sample = (sample * channels[ch].mix_vol) >> 8;

        /* Apply panning */
        int32_t pan = channels[ch].pan;
        int32_t pan_left = (255 - pan);
        int32_t pan_right = pan;

        /* Mix to channels */
        // sample is 16-bit. pan is 8-bit. Result 24-bit. >> 8 back to 16-bit.
        // Multiply by 2 to maintain unity gain at center (128 = 0.5 -> * 2
        // = 1.0)
        mix_left += (sample * pan_left) >> 7;
        mix_right += (sample * pan_right) >> 7;

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
