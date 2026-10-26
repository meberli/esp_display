#pragma once
// Console UART and time helpers on plain ESP-IDF drivers.

#include <stddef.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static inline void sleep_ms(uint32_t ms) { vTaskDelay(ms < 1 ? 1 : pdMS_TO_TICKS(ms)); }

// UART0 (CH340) via driver/uart.h. ESP_LOG shares the port.
void console_begin(uint32_t baud, size_t rx_buf, size_t tx_buf);
void console_set_baud(uint32_t baud);
int console_available(void);
int console_read(void);  // -1 if nothing buffered
size_t console_write(const void* data, size_t n);
void console_puts(const char* s);  // appends "\n"
int console_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void console_flush(void);

uint32_t sys_flash_size(void);
uint32_t sys_psram_size(void);

// main.cpp: creates the network (core 0) and UI (core 1) tasks.
void app_start(void);
