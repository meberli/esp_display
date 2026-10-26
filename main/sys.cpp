#include "sys.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_flash.h"
#include "esp_psram.h"

namespace {
constexpr uart_port_t kPort = UART_NUM_0;
bool g_started = false;
}  // namespace

void console_begin(uint32_t baud, size_t rx_buf, size_t tx_buf) {
  if (!g_started) {
    fflush(stdout);
    if (rx_buf < 256) rx_buf = 256;
    ESP_ERROR_CHECK(uart_driver_install(kPort, static_cast<int>(rx_buf), static_cast<int>(tx_buf < 256 ? 0 : tx_buf),
                                        0, nullptr, 0));
    g_started = true;
  }
  console_set_baud(baud);
}

void console_set_baud(uint32_t baud) {
  uart_wait_tx_done(kPort, pdMS_TO_TICKS(200));
  uart_set_baudrate(kPort, baud);
}

int console_available(void) {
  size_t n = 0;
  uart_get_buffered_data_len(kPort, &n);
  return static_cast<int>(n);
}

int console_read(void) {
  uint8_t c;
  return uart_read_bytes(kPort, &c, 1, 0) == 1 ? c : -1;
}

size_t console_write(const void* data, size_t n) {
  int w = uart_write_bytes(kPort, static_cast<const char*>(data), n);
  return w < 0 ? 0 : static_cast<size_t>(w);
}

void console_puts(const char* s) {
  console_write(s, strlen(s));
  console_write("\n", 1);
}

int console_printf(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return n;
  if (static_cast<size_t>(n) < sizeof(buf)) {
    console_write(buf, n);
    return n;
  }
  char* big = static_cast<char*>(malloc(n + 1));
  if (big == nullptr) return -1;
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  console_write(big, n);
  free(big);
  return n;
}

void console_flush(void) { uart_wait_tx_done(kPort, pdMS_TO_TICKS(2000)); }

uint32_t sys_flash_size(void) {
  uint32_t size = 0;
  esp_flash_get_size(nullptr, &size);
  return size;
}

uint32_t sys_psram_size(void) { return static_cast<uint32_t>(esp_psram_get_size()); }
