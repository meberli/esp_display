# Guition ESP32-4848S040C – Entwicklerdoku

Target `guition` (Standard, ohne `-DPANEL_BOARD`). Projektname bzw. Board-ID `loxone_panel`, Image `build/loxone_panel.bin`. Allgemeines steht in [allgemein.md](allgemein.md).

## Hardware

| | |
|---|---|
| Board | Guition **ESP32-4848S040C_I_Y_1** (1 Relais; `_Y_3` = 3 Relais) |
| MCU | ESP32-S3 N16R8: 16 MB Flash QIO 80 MHz, 8 MB Octal-PSRAM, 2 × 240 MHz |
| Display | 4,0" IPS 480×480, 16-bit RGB, Controller **ST7701** (SPI-Init auf GPIO39/48/47) |
| Touch | **GT911** an I²C (0x5D, 100 kHz), INT/RST nicht verbunden → Polling |
| Konsole | USB-C über CH340 (UART0, GPIO43/44), z. B. `COMx` bzw. `/dev/ttyUSB0` |
| Versorgung | 5 V |
| Bauform | 86-mm-Unterputzdose |

Pins: `main/board_pins_guition.h`. Die vollständige Pin-Tabelle und die Quellen stehen im Haupt-[README](../../README.md#guition-esp32-4848s040c).

## Treiber (`main/panel_hw.cpp`)

- RGB-Timing nach Arduino_GFX (`ESP32_4848S040_86BOX_GUITION`): PCLK 12 MHz, HSYNC FP 10 / Puls 8 / BP 50, VSYNC FP 10 / Puls 8 / BP 20, Init-Sequenz `st7701_type9_init_operations`.
- Framebuffer in PSRAM, Bounce-Buffer 2 × 10 Zeilen im internen RAM.
- Backlight GPIO38 über LEDC-PWM, mit Fade für den Bildschirmschoner.
- Touch-Mapping: `x = raw_y`, `y = 480 − raw_x`. Punktdaten ab Register `0x814F` (Status `0x814E`). Release-Debounce gegen einzelne leere Samples.
- GPIO20 ist Datenleitung des LCD (G1), deshalb gibt es kein USB-CDC und die Konsole läuft über den CH340.

## Board-Funktion: Relais

- `kBoardHasRelay = true`, Relais 1 auf **GPIO40**, aktiv high, beim Boot aus (`panel_relay_begin`).
- In der Kategorie „Schalter“ erscheint es als lokale Kachel. Es kann auch Ziel der Home-Taste „Schalter“ sein (`sw_target = "relay"`).
- Relais 2/3 (GPIO2/1, nur `_Y_3`) werden nicht angesteuert.
- Die Polarität folgt dem ha5dzs-Port (LOW = aus), nicht dem `inverted: true` von ESPHome.
- Belastbarkeit des Relaiskontakts: **offen** (im Repo nicht dokumentiert).

## Flashen

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Startet das Flashen nicht: BOOT halten, Flash starten, BOOT loslassen.

## Release und OTA

- Wird von `ci.yml`, `test-image.yml` (`target: guition` oder `both`) und `release.yml` gebaut.
- Release-Asset `loxone_panel.bin`, das das Panel über die Standard-Update-URL automatisch findet.

## Montage

- Das Wandmodul mit 230 V (Sockel/Frontplatte im Stil Feller EDIZIOdue, 230 V auf einer Seite, Modbus auf der anderen) ist nicht Teil dieses Repos. Konstruktionsdaten (STEP/DXF) liegen ausserhalb. **Offen**, wo sie abgelegt werden.
- Netzspannung darf nur ein Elektroinstallateur mit Bewilligung nach NIV anschliessen. Die Konstruktion ist nicht zertifiziert.
