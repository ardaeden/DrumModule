#ifndef PATTERN_MANAGER_H
#define PATTERN_MANAGER_H

#include "sequencer.h"
#include <stdint.h>

/**
 * @brief Save pattern to a slot
 * @param pattern Pointer to pattern to save
 * @param slot Slot number (1-100)
 * @return 0 on success, -1 on error
 */
int Pattern_Save(Pattern *pattern, uint8_t slot);

/**
 * @brief Load pattern from a slot
 * @param pattern Pointer to load pattern into
 * @param slot Slot number (1-100)
 * @return 0 on success, -1 on error
 */
int Pattern_Load(Pattern *pattern, uint8_t slot);

/**
 * @brief Get list of occupied slots for patterns
 * @param slots Array to store occupied slot numbers
 * @param max_slots Maximum number of slots to find
 * @return Number of occupied slots found
 */
int Pattern_GetOccupiedSlots(uint8_t *slots, int max_slots);

#endif
