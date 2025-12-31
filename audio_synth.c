#include "audio_synth.h"
#include <math.h>

/* Audio Settings */
#define TABLE_SIZE 48 /* 48kHz / 48 = 1000Hz */
#define AMPLITUDE 10000
#define PI 3.14159265f

static int16_t sine_table[TABLE_SIZE];
static uint16_t table_index = 0;

/* Global Buffer Definition */
int16_t audio_buffer[AUDIO_BUFFER_SIZE];

void AudioSynth_Init(void) {
  /* Pre-calculate Sine Table */
  for (int i = 0; i < TABLE_SIZE; i++) {
    float angle = (2.0f * PI * i) / TABLE_SIZE;
    sine_table[i] = (int16_t)(sinf(angle) * AMPLITUDE);
  }
  table_index = 0;
}

void AudioSynth_FillBuffer(int16_t *buffer, uint16_t num_samples) {
  /* num_samples is the total number of int16_t values (L+R interleaved).
   * We need to output num_samples/2 stereo frames.
   * Each stereo frame uses the SAME table entry for both L and R.
   * We advance the table index once per stereo frame.
   */
  for (uint16_t i = 0; i < num_samples; i += 2) {
    int16_t sample = sine_table[table_index];

    /* Left Channel */
    buffer[i] = sample;
    /* Right Channel */
    buffer[i + 1] = sample;

    /* Advance to next table entry (once per stereo frame) */
    table_index++;
    if (table_index >= TABLE_SIZE) {
      table_index = 0;
    }
  }
}
