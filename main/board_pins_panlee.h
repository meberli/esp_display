#pragma once

// Included through board_pins.h (CONFIG_PANEL_BOARD_PANLEE).
//
// Panlee / Wireless-Tag ZX3D95CE01S-TR-V12 (WT32S3-86S family): module
// WT32-S3-WROVER-N16R8 (ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM in the
// chip), 3.95" 480x480 IPS RGB panel with GC9503V controller, FT6336-class
// capacitive touch (FT5x06 register set), onboard RS485 with automatic
// direction control, SHT20 footprint on the I2C bus (0x40, TR variant).
//
// Sources (see docs/entwickler/panlee.md):
//  [DS]  WT32S3-86S / ZX3D95CE01S-TR-4848 datasheet V1.5 EN (Wireless-Tag)
//  [WT]  wireless-tag-com/ZX3D95CE01S-TR-4848 main/board.h + sdkconfig
//  [LGFX] strange-v/ha_deck hd_device_wt32s3_86s/LGFX.hpp (working ESPHome
//        device) + LovyanGFX Panel_GC9503 init list
// The LCD SPI init lines SCLK/MOSI share GPIO45/48 with RGB D0/D1 [DS].

constexpr const char* kBoardName = "Panlee ZX3D95CE01S-TR";
constexpr bool kBoardHasRelay = false;

constexpr int kPanelWidth = 480;
constexpr int kPanelHeight = 480;

// GC9503V 3-wire SPI (9-bit) init bus [DS][WT][LGFX].
constexpr int kPinLcdCs = 38;
constexpr int kPinLcdSck = 45;   // = RGB D0 (B0)
constexpr int kPinLcdMosi = 48;  // = RGB D1 (B1)
// LCD reset is an RC fed by VSYNC [DS]; LGFX pulses GPIO41 as "reset".
constexpr int kPinLcdRstVsync = 41;

// 16-bit RGB565, DATA0..15 = B0..B4, G0..G5, R0..R4 [DS][WT][LGFX].
constexpr int kPinDe = 40;
constexpr int kPinVsync = 41;
constexpr int kPinHsync = 42;
constexpr int kPinPclk = 39;
constexpr int kPinB0 = 45, kPinB1 = 48, kPinB2 = 47, kPinB3 = 0, kPinB4 = 21;
constexpr int kPinG0 = 14, kPinG1 = 13, kPinG2 = 12, kPinG3 = 11, kPinG4 = 16, kPinG5 = 17;
constexpr int kPinR0 = 18, kPinR1 = 8, kPinR2 = 3, kPinR3 = 46, kPinR4 = 10;

// Backlight, active high. GPIO5 per [DS V1.5 EN][WT][LGFX]; an older Chinese
// datasheet says GPIO4 (collides with TP_INT, treated as a typo).
constexpr int kPinBacklight = 5;

// Touch + external I2C (shared with J2 SDA/SCL and the SHT20) [DS][WT][LGFX].
constexpr int kPinTpSda = 15;
constexpr int kPinTpScl = 6;
constexpr int kPinTpInt = 4;   // not used (polled)
constexpr int kPinTpRst = -1;  // RC reset
constexpr uint8_t kShtAddr = 0x40;  // SHT20, only on -TR boards; may be unpopulated

// UART0 on the debug connector and J2 (TX0/RX0).
constexpr int kPinUartTx = 43;
constexpr int kPinUartRx = 44;

// RS485 transceiver with automatic direction (no DE/RE GPIO) [DS]. Also on
// J2 as TX1/RX1 (TXD_EXT/RXD_EXT). Not driven by this firmware yet.
constexpr int kPinRs485Tx = 2;
constexpr int kPinRs485Rx = 1;
constexpr int kPinRs485De = -1;

// J2 general IO [DS]: EXT_IO1..4.
constexpr int kPinExtIo1 = 9;
constexpr int kPinExtIo2 = 20;
constexpr int kPinExtIo3 = 19;
constexpr int kPinExtIo4 = 7;

// No relay on this board.
constexpr int kPinRelay1 = -1;
constexpr int kPinRelay2 = -1;
constexpr int kPinRelay3 = -1;
constexpr int kRelayOnLevel = 1;
constexpr int kRelayOffLevel = 0;
