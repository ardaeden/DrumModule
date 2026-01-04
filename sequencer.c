#include "sequencer.h"
#include "audio_mixer.h"
#include "sequencer_clock.h"
#include <string.h>

/* Sequencer state */
static Pattern current_pattern = {0};
static volatile uint8_t current_step = 0;
static volatile uint8_t playing = 0;
static volatile uint8_t pulse_count = 0;

/* Double buffering for seamless pattern switching */
static Pattern next_pattern_buffer = {0};
static volatile uint8_t next_pattern_ready = 0;
static volatile uint8_t queued_slot = 0;

/* Forward declaration */
/* Forward declaration */
static void sequencer_clock_callback(uint8_t pulse);
static void TriggerCurrentStep(void);

void Sequencer_Init(void) {
  /* Initialize pattern with defaults */
  current_pattern.step_count = 16;
  current_pattern.bpm = 120;
  strncpy(current_pattern.name, "PATTERN 001", sizeof(current_pattern.name));

  /* Clear all steps */
  memset(current_pattern.steps, 0, sizeof(current_pattern.steps));

  /* Initialize clock */
  Clock_Init();
  Clock_SetBPM(current_pattern.bpm);
  Clock_SetCallback(sequencer_clock_callback);

  /* Reset state */
  current_step = 0;
  pulse_count = 0;
  playing = 0;
}

void Sequencer_Start(void) {
  current_step = 0;
  pulse_count = 0;
  playing = 1;

  /* Trigger first step immediately */
  TriggerCurrentStep();

  Clock_Start();
}

void Sequencer_Stop(void) {
  playing = 0;
  Clock_Stop();
  current_step = 0;
  pulse_count = 0;
}

uint8_t Sequencer_IsPlaying(void) { return playing; }

uint8_t Sequencer_GetCurrentStep(void) { return current_step; }

Pattern *Sequencer_GetPattern(void) { return &current_pattern; }

void Sequencer_SetStep(uint8_t channel, uint8_t step, uint8_t value) {
  if (channel < NUM_CHANNELS && step < MAX_STEPS) {
    current_pattern.steps[channel][step] = value;
  }
}

uint8_t Sequencer_GetStep(uint8_t channel, uint8_t step) {
  if (channel < NUM_CHANNELS && step < MAX_STEPS) {
    return current_pattern.steps[channel][step];
  }
  return 0;
}

void Sequencer_ToggleStep(uint8_t channel, uint8_t step) {
  if (channel < NUM_CHANNELS && step < MAX_STEPS) {
    if (current_pattern.steps[channel][step] == 0) {
      current_pattern.steps[channel][step] = 255; /* Full velocity */
    } else {
      current_pattern.steps[channel][step] = 0;
    }
  }
}

void Sequencer_CycleStep(uint8_t channel, uint8_t step) {
  if (channel < NUM_CHANNELS && step < MAX_STEPS) {
    uint8_t current = current_pattern.steps[channel][step];
    if (current == 0)
      current_pattern.steps[channel][step] = 255; /* x1.0 */
    else if (current == 255)
      current_pattern.steps[channel][step] = 128; /* x0.5 */
    else if (current == 128)
      current_pattern.steps[channel][step] = 64; /* x0.25 */
    else if (current == 64)
      current_pattern.steps[channel][step] = 32; /* x0.125 */
    else
      current_pattern.steps[channel][step] = 0; /* OFF */
  }
}

void Sequencer_SetBPM(uint16_t bpm) {
  current_pattern.bpm = bpm;
  Clock_SetBPM(bpm);
}

uint16_t Sequencer_GetBPM(void) { return current_pattern.bpm; }

void Sequencer_SetStepCount(uint8_t count) {
  if (count >= 1 && count <= MAX_STEPS) {
    current_pattern.step_count = count;
  }
}

uint8_t Sequencer_GetStepCount(void) { return current_pattern.step_count; }

void Sequencer_ClearPattern(void) {
  memset(current_pattern.steps, 0, sizeof(current_pattern.steps));
}

/**
 * @brief Helper to trigger samples for current step
 */
static void TriggerCurrentStep(void) {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    if (current_pattern.steps[ch][current_step] > 0) {
      AudioMixer_Trigger(ch, current_pattern.steps[ch][current_step]);
    }
  }
}

/**
 * @brief Clock callback - called at 24 PPQN
 */
static void sequencer_clock_callback(uint8_t pulse) {
  (void)pulse; /* Unused in Phase 1 */

  if (!playing)
    return;

  pulse_count++;

  /* Trigger on every 6th pulse (16th notes at 24 PPQN)
   * 24 PPQN / 4 = 6 pulses per 16th note
   */
  if (pulse_count >= 6) {
    pulse_count = 0;

    /* Advance to next step */
    current_step++;
    if (current_step >= current_pattern.step_count) {
      current_step = 0;

      /* Seamlessly swap pattern if one is queued */
      if (next_pattern_ready) {
        uint16_t current_bpm = current_pattern.bpm;
        memcpy(&current_pattern, &next_pattern_buffer, sizeof(Pattern));
        current_pattern.bpm = current_bpm; /* Ignore loaded BPM */
        next_pattern_ready = 0;
        /* Note: BPM update intentionally disabled per user request */
      }
    }

    /* Trigger audio samples */
    TriggerCurrentStep();
  }
}

void Sequencer_QueuePattern(Pattern *new_pattern, uint8_t slot) {
  memcpy(&next_pattern_buffer, new_pattern, sizeof(Pattern));
  queued_slot = slot;
  next_pattern_ready = 1;
}

uint8_t Sequencer_IsPatternQueued(void) { return next_pattern_ready; }

uint8_t Sequencer_GetQueuedSlot(void) { return queued_slot; }
