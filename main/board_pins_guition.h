#pragma once

// Included through board_pins.h (default board).

// Pin map for the Guition ESP32-4848S040 capacitive line, including the
// one-relay SKU ESP32-4848S040C_I_Y_1.
//
// Display controller ST7701 is named in Guition's specification. The touch
// controller is not named there; published working configs for this line use
// a GT911. Sources and the known-versus-assumed split are in the README.
//
// These values match Arduino_GFX device ESP32_4848S040_86BOX_GUITION
// (Arduino_GFX v1.6.0), ha5dzs/Guition-ESP32-4848S040-platformio, and the
// NorthernMan54 GPIO table. Do not add pins that those sources leave unused.

constexpr const char* kBoardName = "Guition ESP32-4848S040C";
constexpr bool kBoardHasRelay = true;

constexpr int kPanelWidth = 480;
constexpr int kPanelHeight = 480;

// ST7701 3-wire SPI init. DC and MISO are not wired (GFX_NOT_DEFINED).
constexpr int kPinLcdCs = 39;
constexpr int kPinLcdSck = 48;
constexpr int kPinLcdMosi = 47;

// 16-bit RGB (5-6-5) panel.
constexpr int kPinDe = 18;
constexpr int kPinVsync = 17;
constexpr int kPinHsync = 16;
constexpr int kPinPclk = 21;
constexpr int kPinR0 = 11;
constexpr int kPinR1 = 12;
constexpr int kPinR2 = 13;
constexpr int kPinR3 = 14;
constexpr int kPinR4 = 0;
constexpr int kPinG0 = 8;
constexpr int kPinG1 = 20;
constexpr int kPinG2 = 3;
constexpr int kPinG3 = 46;
constexpr int kPinG4 = 9;
constexpr int kPinG5 = 10;
constexpr int kPinB0 = 4;
constexpr int kPinB1 = 5;
constexpr int kPinB2 = 6;
constexpr int kPinB3 = 7;
constexpr int kPinB4 = 15;

// Backlight enable. Digital on, not the relay.
constexpr int kPinBacklight = 38;

// GT911 I2C. GPIO45 is a strapping pin; every published config uses it as SCL
// after boot. INT and RST are not in the published GPIO tables.
constexpr int kPinTpSda = 19;
constexpr int kPinTpScl = 45;
constexpr int kPinTpInt = -1;
constexpr int kPinTpRst = -1;

// TF card. Not initialized by this firmware. MOSI/SCK are shared with the
// ST7701 init bus.
constexpr int kPinSdCs = 42;
constexpr int kPinSdMiso = 41;
constexpr int kPinSdMosi = 47;
constexpr int kPinSdSck = 48;

// UART0 through the onboard USB-UART bridge (CH340). GPIO43 TX, GPIO44 RX.
constexpr int kPinUartTx = 43;
constexpr int kPinUartRx = 44;

// Relays. The C_I_Y_1 SKU is the one-way model: relay 1 is GPIO40.
// GPIO1 and GPIO2 are the extra relays on the three-way SKU (C_I_Y_3).
// This firmware drives relay 1 only. It does not configure GPIO1 or GPIO2.
//
// Off level is from ha5dzs setup(): pinMode(RELAY1) then digitalWrite(RELAY1, LOW)
// with the comment "All off, by default." HIGH turns this relay on.
// The ESPHome device page sets GPIO40 inverted: true (logical ON drives the
// pin low). That catalog entry disagrees; this firmware follows the ha5dzs write.
constexpr int kPinRelay1 = 40;
constexpr int kPinRelay2 = 2;
constexpr int kPinRelay3 = 1;
constexpr int kRelayOnLevel = 1;
constexpr int kRelayOffLevel = 0;

static_assert(kPinRelay1 != kPinBacklight, "relay pin collided with backlight");
static_assert(kPinRelay1 != kPinTpSda, "relay pin collided with touch");
static_assert(kPinRelay1 != kPinPclk, "relay pin collided with the RGB bus");
static_assert(kPinRelay2 != kPinBacklight, "relay pin collided with backlight");
static_assert(kPinRelay3 != kPinBacklight, "relay pin collided with backlight");
