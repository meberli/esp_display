// See rp_link.h.
#include "rp_link.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_PANEL_BOARD_SENSECAP_D1
#include "board_pins_sensecap_d1.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "sys.h"
#endif

namespace {

enum class CmdKind : uint8_t { Open, Close, Arm, Stop, View };
struct Cmd {
  CmdKind kind;
  bool run;
  char text[RPS_MAX_CMD];
};

QueueHandle_t g_cmd = nullptr;
QueueHandle_t g_ui = nullptr;
TaskHandle_t g_task = nullptr;
RpLinkStats g_stats{};

void post(const Cmd& c) {
  if (g_cmd != nullptr) xQueueSend(g_cmd, &c, 0);
}

#if CONFIG_PANEL_BOARD_SENSECAP_D1
constexpr uart_port_t kPort = UART_NUM_2;

void ui_post(const RpUiMsg& m) {
  if (g_ui == nullptr) return;
  if (xQueueSend(g_ui, &m, 0) != pdTRUE) {
    EXT_RAM_BSS_ATTR static RpUiMsg drop;
    xQueueReceive(g_ui, &drop, 0);  // keep the newest
    xQueueSend(g_ui, &m, 0);
  }
}

bool send_text(const char* text) {
  uint8_t frame[RPS_MAX_CMD + 8];
  size_t n = rps_cmd_frame(text, frame, sizeof(frame));
  if (n == 0) {  // too long, not printable, or would contain 0xA3: never sent
    ++g_stats.tx_refused;
    console_printf("RPLINK refused command\n");
    return false;
  }
  uart_write_bytes(kPort, frame, n);
  ++g_stats.tx_cmds;
  return true;
}

void task(void*) {
  uart_config_t cfg = {};
  cfg.baud_rate = 115200;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  ESP_ERROR_CHECK(uart_driver_install(kPort, 8192, 0, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(kPort, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(kPort, kPinRpTx, kPinRpRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  uart_flush_input(kPort);

  EXT_RAM_BSS_ATTR static uint8_t acc[RPS_MAX_FRAME + 64];
  EXT_RAM_BSS_ATTR static uint8_t dec[RPS_MAX_FRAME + 64];
  EXT_RAM_BSS_ATTR static RpUiMsg m;
  EXT_RAM_BSS_ATTR static Cmd c;
  EXT_RAM_BSS_ATTR static char last_arm[RPS_MAX_CMD];
  size_t an = 0;
  bool open = false, run = false, armed = false, caps = false;
  int hello_tries = 0;
  TickType_t hello_t = 0;
  uint8_t rx[256];

  for (;;) {
    while (xQueueReceive(g_cmd, &c, 0) == pdTRUE) {
      switch (c.kind) {
        case CmdKind::Open:
          open = true;
          if (!caps) {
            hello_tries = 0;
            hello_t = 0;
          }
          break;
        case CmdKind::Close:
          if (armed || run) send_text("STOP");
          open = run = armed = false;
          break;
        case CmdKind::Arm:
          snprintf(last_arm, sizeof(last_arm), "%s", c.text);
          run = c.run;
          armed = send_text(last_arm);
          break;
        case CmdKind::Stop:
          run = armed = false;
          send_text("STOP");
          break;
        case CmdKind::View:
          if (!armed) send_text(c.text);
          break;
      }
    }
    if (open && !caps && hello_tries < 6 && xTaskGetTickCount() - hello_t > pdMS_TO_TICKS(1500)) {
      hello_t = xTaskGetTickCount();
      if (++hello_tries == 6) {
        memset(&m, 0, offsetof(RpUiMsg, f));
        m.kind = RpMsgKind::NoAnswer;
        ui_post(m);
      } else {
        send_text("HELLO");
      }
    }
    int n = uart_read_bytes(kPort, rx, sizeof(rx), pdMS_TO_TICKS(open ? 10 : 200));
    for (int i = 0; i < n; ++i) {
      if (rx[i] != 0) {
        if (an < sizeof(acc)) acc[an++] = rx[i];
        else an = sizeof(acc) + 1;  // overflow: drop until the next delimiter
        continue;
      }
      size_t len = an <= sizeof(acc) ? rps_cobs_decode(acc, an, dec, sizeof(dec)) : 0;
      an = 0;
      if (len == 0) {
        ++g_stats.rx_bad;
        continue;
      }
      uint8_t t = dec[0];
      if (t >= 0xB0 && t <= 0xB5) {  // Seeed sensor packets: not used by the panel
        ++g_stats.rx_sensor;
        continue;
      }
      memset(&m, 0, offsetof(RpUiMsg, f));
      if (t == RPS_T_CAPS && rps_parse_caps(dec, len, &m.caps)) {
        caps = g_stats.caps = true;
        m.kind = RpMsgKind::Caps;
      } else if (t == RPS_T_STATUS && len >= 4) {
        m.kind = RpMsgKind::Status;
        m.state = dec[2];
        m.flags = dec[3];
        // The idle STATUS right after a capture frame is ignored (the next
        // ARM is already on its way); a normal-trigger timeout or an overrun
        // ends the capture without a frame: try again while running.
        if (m.state == 0 && (m.flags & (RPS_F_TIMEOUT | RPS_F_OVERRUN))) {
          armed = false;
          if (run && open) armed = send_text(last_arm);
        }
      } else if (t == RPS_T_ERR) {
        m.kind = RpMsgKind::Err;
        size_t k = len - 1 < sizeof(m.text) - 1 ? len - 1 : sizeof(m.text) - 1;
        memcpy(m.text, dec + 1, k);
        m.text[k] = 0;
      } else if ((t == RPS_T_ANALOG || t == RPS_T_LOGIC) && rps_parse_frame(dec, len, &m.f)) {
        m.kind = RpMsgKind::Frame;
        if (!(m.f.flags & RPS_F_VIEW)) {
          armed = false;
          if (run && open) armed = send_text(last_arm);  // the RP2040 sends STATUS idle right after
        }
      } else {
        ++g_stats.rx_bad;
        continue;
      }
      ++g_stats.rx_frames;
      if (open) ui_post(m);
    }
  }
}
#endif

Cmd* cmd_buf() {
  EXT_RAM_BSS_ATTR static Cmd c;  // UI task only
  memset(&c, 0, sizeof(c));
  return &c;
}

}  // namespace

bool rp_link_supported() {
#if CONFIG_PANEL_BOARD_SENSECAP_D1
  return true;
#else
  return false;
#endif
}

void rp_link_open() {
#if CONFIG_PANEL_BOARD_SENSECAP_D1
  if (g_task == nullptr) {
    g_cmd = xQueueCreateWithCaps(6, sizeof(Cmd), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_ui = xQueueCreateWithCaps(3, sizeof(RpUiMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_cmd == nullptr || g_ui == nullptr) return;
    // Core 0 with the network; low priority (below co4en 2).
    xTaskCreatePinnedToCoreWithCaps(task, "rplink", 6 * 1024, nullptr, 1, &g_task, 0,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Open;
  post(*c);
#endif
}

void rp_link_close() {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Close;
  post(*c);
}

void rp_link_arm(const RpArm& a) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Arm;
  c->run = a.run;
  snprintf(c->text, sizeof(c->text), "%s", a.text);
  post(*c);
}

void rp_link_stop() {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Stop;
  post(*c);
}

void rp_link_view(const char* view_text) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::View;
  snprintf(c->text, sizeof(c->text), "%s", view_text);
  post(*c);
}

bool rp_link_take(RpUiMsg* out) { return g_ui != nullptr && xQueueReceive(g_ui, out, 0) == pdTRUE; }

RpLinkStats rp_link_stats() { return g_stats; }
