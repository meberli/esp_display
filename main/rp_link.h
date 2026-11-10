#pragma once
// SenseCAP D1 only: UART link to the on-board RP2040 for the Playground scope
// (protocol: rp2040/libraries/RpScope/src/rp_scope.h). ESP32-S3 UART2 on
// GPIO19 (TX) / GPIO20 (RX), 115200 8N1, COBS frames.
//
// The UART is only installed when the scope page is opened for the first
// time, so a panel that never opens it never talks to the RP2040. The link
// task runs on core 0; the UI only posts commands (rp_link_*) and drains
// rp_link_take() from its own LVGL timer (core 1).
// The scope never sends Seeed's shutdown command (0xA3); rps_cmd_frame()
// refuses any frame containing that byte.

#include <stddef.h>
#include <stdint.h>

#include "rp_scope.h"

bool rp_link_supported();  // board has the RP2040 link (D1)

void rp_link_open();   // install UART + task once, send HELLO (until CAPS)
void rp_link_close();  // stop any capture; the task idles

struct RpArm {
  char text[RPS_MAX_CMD];  // "ARM ..." (scope::arm_text)
  bool run;                // re-arm after every capture until stop
};
void rp_link_arm(const RpArm& a);
void rp_link_stop();
void rp_link_view(const char* view_text);  // "VIEW ..." on the stored capture

enum class RpMsgKind : uint8_t { Caps, Status, Frame, Err, NoAnswer };
struct RpUiMsg {
  RpMsgKind kind;
  uint8_t state, flags;  // Status
  rps_caps_t caps;       // Caps
  char text[64];         // Err
  rps_frame_t f;         // Frame
};
bool rp_link_take(RpUiMsg* out);  // UI task: next message, false if none

struct RpLinkStats {
  uint32_t rx_frames, rx_bad, rx_sensor, tx_cmds, tx_refused;
  bool caps;
};
RpLinkStats rp_link_stats();  // racy read, diagnostics only
