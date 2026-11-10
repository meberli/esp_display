// Playground scope on the SenseCAP Indicator RP2040: analog (ADC + DMA) and
// logic (PIO + DMA) capture at the Grove connectors, results to the ESP32-S3
// over the existing COBS UART link. Protocol: libraries/RpScope/src/rp_scope.h.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef void (*scope_send_fn)(const uint8_t* data, size_t len);

void scope_begin(scope_send_fn send);
// One 'S' packet from the ESP32 (without the type byte).
void scope_command(const uint8_t* text, size_t len);
// Call from loop(); true while a capture runs (caller must not block then).
bool scope_poll();
// A scope command arrived in the last `ms` milliseconds (page open on the ESP32).
bool scope_recently_used(uint32_t ms);
