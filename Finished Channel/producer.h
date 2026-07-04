#pragma once

#include "shared.h"

/*
 * Controlled same-host scheduling-channel demonstration.
 *
 * Bit 1: remain CPU-runnable for the entire slot.
 * Bit 0: sleep until the slot's absolute end time.
 *
 * Both producer and consumer must use the same 
 *          1. CPU number
 *          2. slot length
 *          3. Preamble pattern
 *          4. CRC Polynomial
 */

#define DEFAULT_REPEATS          5
#define STARTUP_DELAY_NS         500000000ULL
#define INTERFRAME_IDLE_SLOTS    6U

void usage(char *argv0);

void sleepUntil(uint64_t deadline_ns);
void busyUntil(uint64_t deadline_ns);
uint64_t alignTime(uint64_t value, uint64_t quantum);

void signalBit(uint8_t bit, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex);
void signalByteBIG(uint8_t byte, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex);
void signalFrame(const uint8_t *payload, size_t length, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex);
