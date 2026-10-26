#pragma once

// Seeed SenseCAP Indicator D1: ESP32-S3 (QFN56 rev 0.1, 8 MB octal PSRAM),
// 8 MB flash (Winbond ef/4017), 4" 480x480 ST7701S RGB panel, FT6336U touch,
// PCA9535 I/O expander, RP2040 coprocessor (not used, never commanded).
// Values from Seeed's BSP (SenseCAP_Indicator_ESP32 components/bsp,
// sensecap_indicator_board.c / lcd_panel_config.c, screen "GX").

constexpr const char* kBoardName = "SenseCAP Indicator D1";
constexpr bool kBoardHasRelay = false;

constexpr int kPanelWidth = 480;
constexpr int kPanelHeight = 480;

// ST7701S 3-wire SPI: SCK/MOSI on GPIOs, CS and RESET on the expander.
constexpr int kPinLcdSck = 41;
constexpr int kPinLcdMosi = 48;
constexpr int kExpLcdCs = 4;
constexpr int kExpLcdRst = 5;
constexpr int kExpTpRst = 7;
constexpr int kExpRp2040Rst = 8;  // held high (released) exactly as Seeed's firmware does

// 16-bit RGB565. Seeed DATA0..15 = B0..B4, G0..G5, R0..R4.
constexpr int kPinDe = 18;
constexpr int kPinVsync = 17;
constexpr int kPinHsync = 16;
constexpr int kPinPclk = 21;
constexpr int kPinB0 = 15, kPinB1 = 14, kPinB2 = 13, kPinB3 = 12, kPinB4 = 11;
constexpr int kPinG0 = 10, kPinG1 = 9, kPinG2 = 8, kPinG3 = 7, kPinG4 = 6, kPinG5 = 5;
constexpr int kPinR0 = 4, kPinR1 = 3, kPinR2 = 2, kPinR3 = 1, kPinR4 = 0;

// Backlight: GPIO45, active high (LEDC PWM).
constexpr int kPinBacklight = 45;

// Shared I2C bus: PCA9535 (0x20, fallback 0x39) and FT6336U (0x48, fallback 0x38).
constexpr int kPinTpSda = 39;
constexpr int kPinTpScl = 40;
constexpr uint8_t kExpAddr1 = 0x20;
constexpr uint8_t kExpAddr2 = 0x39;

constexpr int kPinUartTx = 43;
constexpr int kPinUartRx = 44;

// No relay on this board (capability kBoardHasRelay = false).
constexpr int kPinRelay1 = -1;
constexpr int kPinRelay2 = -1;
constexpr int kPinRelay3 = -1;
constexpr int kRelayOnLevel = 1;
constexpr int kRelayOffLevel = 0;
