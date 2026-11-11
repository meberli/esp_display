# Seeed SenseCAP Indicator D1 – Entwicklerdoku

Target `sensecap_d1`. Projektname bzw. Board-ID `loxone_panel_sensecap_d1`, Image `build-d1/loxone_panel_sensecap_d1.bin`. Allgemeines steht in [allgemein.md](allgemein.md).

## Hardware

| | |
|---|---|
| MCU | ESP32-S3, 8 MB Octal-PSRAM, 8 MB Flash (Winbond), **DIO** (QIO auf diesem Board nicht verifiziert) |
| Display | 4" 480×480 RGB, Controller **ST7701S**. SPI-Init auf GPIO41/48, CS und Reset über den I/O-Expander |
| I/O-Expander | PCA9535 auf I²C (0x20, Fallback 0x39): LCD-CS (4), LCD-Reset (5), Touch-Reset (7), RP2040-Reset (8) |
| Touch | **FT6336U** am selben Bus (0x48, Fallback 0x38), SDA GPIO39 / SCL GPIO40 |
| Backlight | GPIO45, LEDC-PWM |
| Relais | keines |
| Koprozessor | RP2040, UART zum ESP32: ESP GPIO19 (TX) → RP GPIO17, RP GPIO16 → ESP GPIO20 (RX) |
| Konsole ESP32 | UART0 (GPIO43/44) über CH340, unter Linux `/dev/ttyUSB0` |
| RP2040 USB | eigene USB-CDC-Schnittstelle, unter Linux `/dev/ttyACM*` |

Pins: `main/board_pins_sensecap_d1.h`, Werte aus Seeeds BSP (`SenseCAP_Indicator_ESP32`, Screen „GX“).

**Achtung serielle Konsole:** Linux setzt beim Öffnen von `/dev/ttyUSB0` DTR/RTS, und das resettet den ESP32 (Log zeigt `rst:POWERON`). Für Logs ohne Reset einen Leser verwenden, der DTR/RTS nie aktiviert (z. B. `tools/diag_log.py --noreset`). Ob `stty -hupcl` allein reicht, ist **offen**.

## Treiber (`main/panel_hw_sensecap_d1.cpp`)

- PCLK **16 MHz** (ca. 56 Hz). Seeed nutzt 18 MHz mit PSRAM 120 MHz ohne Bounce-Buffer. Hier läuft PSRAM mit 80 MHz und wird mit WLAN/LVGL geteilt.
- Bounce-Buffer **2 × 20 Zeilen**. Mit 10 Zeilen gab es etwa alle 12 s einen verschobenen Frame (verspäteter Bounce-EOF-ISR). Die Firmware zählt solche Ereignisse für `DIAGFB`.
- Der RP2040-Reset am Expander wird freigegeben (high), wie in Seeeds Firmware.

## sdkconfig und Partitionen

`sdkconfig.defaults.sensecap`: `CONFIG_PANEL_BOARD_SENSECAP_D1=y`, Flash DIO 8 MB, `partitions_sensecap.csv` (2 × 3,375 MB OTA). Siehe [allgemein.md](allgemein.md#31-partitionstabellen).

Flash-Backups des D1 enthalten NVS mit WLAN-Daten und sind gitignored (`sensecap_backup/`, `*_backup*.bin`). Nie committen.

## Board-Funktion: Oszi / Logik (RP2040)

Ein Oszilloskop und ein Logik-Analysator. Gemessen wird vom RP2040 an den Grove-Buchsen, angezeigt am ESP32.

### Komponenten

| Teil | Ort | Aufgabe |
|---|---|---|
| RP2040-Sketch | `rp2040/indicator_rp2040_scope/` | Seeeds Werks-Sketch `indicator_rp2040` v1.0.1 (**GPL-3.0**) plus `scope_rp2040.*`. Version `v1.0.1+scope-1.0`, CAPS-Kennung `scope-1.0` |
| Protokoll-Bibliothek | `rp2040/libraries/RpScope/src/rp_scope.{h,c}` | reines C99: Protokoll, COBS, Trigger, Messungen, Dezimierung. Wird von RP2040, ESP32 und Host-Test gemeinsam genutzt |
| UART-Link | `main/rp_link.{h,cpp}` | Task `rplink` (Core 0, Prio 1), UART2. Wird erst beim ersten Öffnen der Seite installiert |
| Planung/Rendering | `main/scope_view.{h,cpp}` | Zeitbasis, Kommandotext, Messwert-Texte, Plot als RGB565-Puffer |
| UI | `main/panel_ui_scope.inc` | Seite „Oszi / Logik“, Home-Taste (nur D1) |
| Host-Test | `tools/test_scope.cpp` | läuft in CI |

`rp_link_supported()` ist nur auf dem D1 `true`. Nur dann zeigt die Startseite eine fünfte Taste (Oszi) vor „Einstellungen“. Ein D1, der die Seite nie öffnet, spricht nie mit dem RP2040.

### Eingänge (max. 3,3 V)

| Kanal | Grove-Buchse | Kabel | RP2040 |
|---|---|---|---|
| Oszi A0 | ADC (links) | gelb | GPIO26 (ADC0) |
| Oszi A1 | ADC (links) | weiss | GPIO27 (ADC1) |
| Logik D0 | I²C (rechts) | weiss (SDA) | GPIO20 |
| Logik D1 | I²C (rechts) | gelb (SCL) | GPIO21 |
| Logik D2 | ADC (links) | gelb | GPIO26 |
| Logik D3 | ADC (links) | weiss | GPIO27 |

- A0/A1 und D2/D3 teilen sich die Pins.
- GPIO20/21 sind der interne I²C-Bus des RP2040 (Sensoren, Pull-ups). Der Logik-Analysator liest dort nur mit, ein fremdes Signal stört die Sensoren.
- Die Pins werden nur gelesen, ihre Funktion wird nicht umgeschaltet.
- Die Lage der Stifte in der Buchse ist bei Seeed widersprüchlich dokumentiert. Massgebend ist die Kabelfarbe (Grove-Standard: Pin 1 gelb, 2 weiss, 3 rot = VCC 3,3 V, 4 schwarz = GND).

### Leistungsdaten

- **Analog:** ADC 12 Bit, 0–3,3 V, 500 kS/s (48 MHz / 96). Mit zwei Kanälen im Wechsel je 250 kS/s. Bis 61'440 Samples pro Aufnahme. Sauber darstellbar bis ca. 50 kHz. Software-Flankentrigger mit Hysterese (ca. 20 mV), Pre-Trigger 10/50/90 %, Auto/Normal.
- **Logik:** PIO liest GPIO20–27 mit clk_sys/div, max. **100 MS/s** (clk_sys 200 MHz, div 2), 4 Kanäle, bis 126'976 Samples (1,27 ms bei 100 MS/s). Hardware-Flankentrigger über eine zweite State Machine, das exakte Sample wird danach in den Daten gesucht.
- Ein gemeinsamer 128-KiB-Ringpuffer, gefüllt per DMA mit Kontroll-Kanal ohne IRQs.
- Der RP2040 sendet 450 dezimierte Spalten (min/max pro Spalte). Die Messwerte (Vmin/Vmax/Vmean/f bzw. f/Duty/Flanken) berechnet er auf der vollen Aufnahme.
- Seeeds Sensorrunde (alle 5 s) wartet, solange eine Aufnahme läuft. Wurde der Scope in den letzten 15 s benutzt, lässt die Hauptschleife das `delay(10)` weg und antwortet schneller.

### UART-Protokoll (`rp_scope.h`, `RPS_PROTO_VER 1`)

- UART **115200 8N1**. Jeder Frame ist **COBS**-kodiert und endet mit einem `0x00` (PacketSerial-Framing wie bei Seeed). Das erste dekodierte Byte ist der Pakettyp.
- Seeeds Sensorpakete `0xB0`–`0xB5` bleiben unverändert. Der ESP32 zählt sie nur (`rx_sensor`) und zeigt sie nicht an.
- **ESP32 → RP2040:** Typ `'S'` (0x53) + ASCII-Text, max. 159 Zeichen:

| Befehl | Bedeutung |
|---|---|
| `S HELLO` | Verbindung prüfen. Der ESP32 wiederholt das, bis CAPS kommt |
| `S ARM m=<a\|l> ch=<0\|1\|2> r=<Rate> n=<Samples> p=<Pre %> e=<r\|f\|n> l=<Level 12 Bit> s=<Quelle> t=<a\|n> w=<Spalten> o=<Timeout ms>` | Aufnahme starten. `m` analog/logic, `ch` 0=GPIO26, 1=GPIO27, 2=beide, `e` steigend/fallend/kein Trigger, `t` auto/normal, `o` auf 10–5000 ms begrenzt |
| `S VIEW o=<erstes Sample> n=<Samples> w=<Spalten>` | Ausschnitt der gespeicherten Aufnahme neu senden (Zoom/Pan) |
| `S STOP` | Aufnahme abbrechen |

- **Schutz:** `rps_cmd_frame()` lehnt jeden Frame ab, der das Byte **0xA3** enthalten würde (Seeeds `PKT_TYPE_CMD_SHUTDOWN`). Ein druckbarer Befehl unter 160 Bytes erzeugt nie ein Byte ≥ 0xA1. Abgelehnte Befehle zählt `tx_refused`.
- **RP2040 → ESP32:** binär, Little Endian:

| Typ | Inhalt |
|---|---|
| `0xC0` CAPS | Antwort auf HELLO: Protokollversion, sys_hz, max. ADC-Rate, max. Logik-Rate, max. Samples analog/logik, Kanäle, FW-Kennung (12 Zeichen) |
| `0xC1` STATUS | `{type, ver, state, flags}`, z. B. 1 = scharf, 0 = fertig/gestoppt |
| `0xC2` ANALOG | Aufnahme analog: seq, flags, Kanalmodus, Spalten, Rate, n, Trigger-Position, View-Offset/-Länge, Messwerte je Kanal, `amin/amax[ch][col]` (12 Bit >> 4) |
| `0xC3` LOGIC | Aufnahme Logik: wie oben, Messwerte je Kanal (Flanken, f, Duty ‰, Pegel), `lbits[col]` (Bit 2c = low gesehen, 2c+1 = high gesehen) |
| `0xC4` ERR | Fehlertext |

- Flags: `TRIGGERED 0x01`, `AUTO 0x02` (kein Trigger, freilaufend), `OVERRUN 0x04`, `VIEW 0x08`, `TIMEOUT 0x10` (Normal-Modus ohne Trigger).
- Im Modus „Run“ armt der ESP32 nach jeder Aufnahme neu, bis „Stop“. „Single“ armt einmal.
- Antwortet der RP2040 nicht, zeigt die Seite „RP2040 antwortet nicht – Scope-Firmware fehlt?“. Der Rest der Firmware läuft normal weiter.

### UI (`panel_ui_scope.inc`)

- Die Seite scrollt nie. Oben eine 44-px-Leiste: Zurück, Oszi | Logik, Run / Single / Stop, Einstellungen. Darunter eine Statuszeile mit Hinweis „max 3.3 V“.
- Plot über die volle Breite: **480 × 336** px, 10 × 8 Divisions. Messwerte liegen direkt auf dem Plot.
- Unten: Zeitbasis −/+, Zoom −/+, Pan ‹/›.
- Einstellungs-Overlay: Kanäle (A0, A1, A0 + A1), V/div (100, 200, 500, 1000, 2000 mV) und Position, Trigger (steigend, fallend, aus), Quelle, Level, Modus (Auto/Normal), Pre (10/50/90 %).
- Zeitbasen: analog 10 µs … 200 ms/div, Logik 100 ns … 10 ms/div. Standard 200 µs/div bzw. 10 µs/div.
- Zurück: Zurück-Pfeil oder Wischen nach rechts.

### RP2040-Firmware bauen

```sh
cd rp2040
arduino-cli core install rp2040:rp2040          # earlephilhower arduino-pico, getestet 6.3.2
arduino-cli lib install PacketSerial "Sensirion I2C SGP40" "Sensirion I2C SCD4x" \
    "Sensirion Gas Index Algorithm" "Sensirion Core" Seeed_Arduino_AHT20
arduino-cli compile -b rp2040:rp2040:seeed_indicator_rp2040:freq=200 \
    --libraries libraries --output-dir out indicator_rp2040_scope
```

Ausgabe: `out/indicator_rp2040_scope.ino.uf2`. Wichtig ist `freq=200`, denn 100 MS/s brauchen clk_sys 200 MHz.

### RP2040 flashen (UF2)

1. RP2040 in den USB-Bootloader bringen (BOOTSEL, siehe Seeed-Wiki zum SenseCAP Indicator RP2040). Er erscheint dann als Laufwerk `RPI-RP2`.
2. `indicator_rp2040_scope.ino.uf2` auf das Laufwerk kopieren. Der RP2040 startet neu.
3. Zurück zur Werks-Firmware: Seeeds Werks-UF2 genauso kopieren.

Die genaue Taste bzw. Abfolge für BOOTSEL am Gehäuse ist hier nicht dokumentiert (**offen**, Seeed-Wiki). Ob eine Übertragung über die laufende CDC-Schnittstelle (`/dev/ttyACM*`, 1200-Baud-Reset von arduino-pico) zuverlässig klappt, ist im Repo nicht festgehalten (**offen**).

Die ESP32-Firmware braucht den Scope-Sketch nicht. Ohne ihn bleibt nur die Oszi-Seite ohne Verbindung.

### Lizenz

- `rp2040/indicator_rp2040_scope/` ist **GPL-3.0-only** (`LICENSE`, `NOTICE`), weil der Sketch auf Seeeds GPL-3.0-Sketch `indicator_rp2040` v1.0.1 aufbaut. SPDX-Kopf in `.ino`, `scope_rp2040.cpp/.h`.
- `rp2040/libraries/RpScope/` ist eigener Code unter **MIT** (`LICENSE`, SPDX `MIT`). Geprüft: kein aus dem Seeed-Sketch kopierter Code (eigene COBS-Implementierung, Seeed nutzt PacketSerial). Übernommen ist nur die Protokolltatsache, dass `0xA3` Seeeds Shutdown-Kommando ist.
- Die ESP32-Firmware (MIT) enthält RpScope, aber keinen GPL-Code. Sketch und Firmware sind getrennte Programme, die nur über UART kommunizieren.
- Übersicht aller Drittlizenzen: [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## Release und OTA

- Wird von `ci.yml`, `test-image.yml` (Standard-`target` `sensecap_d1`) und `release.yml` gebaut.
- Release-Asset `loxone_panel_sensecap_d1.bin`. Mit der Standard-GitHub-URL findet das Panel sein Asset automatisch (`ota_board_asset`). Die URL muss nicht umgestellt werden.
- Das RP2040-Image wird **nicht** per OTA verteilt. Es wird nur manuell per UF2 installiert.
