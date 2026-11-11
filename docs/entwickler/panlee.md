# Panlee ZX3D95CE01S-TR-V12 – Entwicklerdoku

Target `panlee`. Projektname bzw. Board-ID `loxone_panel_panlee`, Image `build-panlee/loxone_panel_panlee.bin`. Allgemeines steht in [allgemein.md](allgemein.md).

## Hardware

| | |
|---|---|
| Board | Panlee / Wireless-Tag ZX3D95CE01S-TR-V12 (Familie WT32S3-86S) |
| Modul | WT32-S3-WROVER-N16R8: ESP32-S3 (ESP32-S3R8), 16 MB Quad-Flash, 8 MB eingebettetes Octal-PSRAM (esptool: „Embedded PSRAM 8MB (AP_3v3)“) |
| Display | 3,95" IPS 480×480, 16-bit RGB, Controller **GC9503V** (3-Wire-SPI-Init, 9 Bit) |
| Touch | FT6336-Klasse (FT5x06-Registersatz) auf I²C 0x38 (Fallback 0x48), 400 kHz, gepollt (INT GPIO4 ungenutzt) |
| Sensor | SHT20-Footprint auf I²C 0x40 (nur TR-Variante, evtl. unbestückt) |
| RS485 | Transceiver mit automatischer Richtungsumschaltung, TX GPIO2 / RX GPIO1, auch auf J2 |
| Relais | keines (`kBoardHasRelay = false`) |
| Konsole | UART0 GPIO43/44 auf dem Debug-Stecker und J2 |
| Speisung | → offen, Angabe Michael (Spannung, Anschluss/Klemme) |
| Unterputzdose | → offen, Angabe Michael (passende Dose) |
| Montage | → offen, Angabe Michael (Befestigung, Rahmen) |
| Boot-/Reset-Tasten bzw. Pads | → offen, Angabe Michael (Lage von IO0/BOOT und RESET auf der Platine) |

Quellen laut `main/board_pins_panlee.h`: Wireless-Tag-Datenblatt WT32S3-86S / ZX3D95CE01S-TR-4848 V1.5 EN, `wireless-tag-com/ZX3D95CE01S-TR-4848` (`main/board.h`, `sdkconfig`) und `strange-v/ha_deck` (LGFX). Der Kommentar in `board_pins_panlee.h` verweist auf dieses Dokument.

### Pins (`main/board_pins_panlee.h`)

| Funktion | GPIO |
|---|---|
| LCD SPI CS / SCK / MOSI | 38 / 45 / 48 (SCK und MOSI = RGB B0/B1, nach dem Init an das RGB-Peripheral zurückgegeben) |
| LCD Reset | RC an VSYNC, wird über GPIO41 gepulst |
| DE, VSYNC, HSYNC, PCLK | 40, 41, 42, 39 |
| B0–B4 | 45, 48, 47, 0, 21 |
| G0–G5 | 14, 13, 12, 11, 16, 17 |
| R0–R4 | 18, 8, 3, 46, 10 |
| Backlight | 5, aktiv high (älteres chinesisches Datenblatt: GPIO4, als Tippfehler behandelt) |
| Touch/I²C SDA, SCL, INT | 15, 6, 4 |
| UART0 TX, RX | 43, 44 |
| RS485 TX, RX | 2, 1 (keine DE/RE-Leitung) |
| J2 EXT_IO1–4 | 9, 20, 19, 7 |

## Treiber (`main/panel_hw_panlee.cpp`)

- GC9503V-Init per Bit-Banging über die RGB-Datenleitungen B0/B1, danach Rückgabe an `LCD_DATA_OUT0/1` (wie LGFX).
- RGB-Timing nach ha_deck: HSYNC FP 8 / Puls 10 / BP 50, VSYNC FP 8 / Puls 10 / BP 20. PCLK 12 MHz wie Guition (LGFX nutzt 15 MHz) für Reserve bei der PSRAM-Bandbreite.
- Ein Framebuffer in PSRAM, Bounce-Buffer **2 × 20 Zeilen** (ca. 38 KB internes DMA-RAM).
- Backlight GPIO5 über LEDC, 10 Bit.
- Touch-Rohkoordinaten werden direkt verwendet (keine Rotation).
- SHT20 wird beim Start nur erkannt und in `DIAGHW` gemeldet (`sht20=0/1`). Messwerte werden **nicht** angezeigt.
- RS485 und die J2-IOs werden von der Firmware **nicht** angesteuert.

## sdkconfig

`sdkconfig.defaults.panlee` setzt nur `CONFIG_PANEL_BOARD_PANLEE=y`. Flash QIO 16 MB, Octal-PSRAM 80 MHz und `partitions.csv` entsprechen dem Guition.

## Bekannte Punkte

- **Flackern:** Commit `b2159ca` (idempotente Label-Updates, stille Loxone-Retries, 20-Zeilen-Bounce-Buffer) adressiert das Flackern. Rückmeldung zum Verhalten mit 0.9.10 steht noch aus (**offen**).
- **WLAN/Loxone optional:** Das Panel startet ohne beides und lässt alles am Gerät eintragen (gilt für alle Boards, wurde für Panlee eingeführt).

## Bauen und Flashen

```sh
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee set-target esp32s3
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee build
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee -p <PORT> flash monitor
```

### Flashen über USB-Seriell-Adapter

- Adapter: USB-Seriell-Wandler mit 3,3-V-Pegel, z. B. FTDI FT232R, an UART0 (TX GPIO43, RX GPIO44, GND) am Debug-Stecker.
- Am Adapter sind nur **RTS/CTS** verdrahtet, **kein DTR**. Der automatische Reset in den Bootloader von esptool funktioniert deshalb nicht. Bootmodus von Hand: **IO0 (BOOT) gedrückt halten, RESET kurz drücken, IO0 loslassen**, dann flashen. Nach dem Flashen RESET drücken, damit die App startet.
- Lage der Boot-/Reset-Tasten bzw. Pads auf der Platine: → offen, Angabe Michael.
- **Nur die App flashen** (`idf.py ... app-flash`, schreibt ins aktuelle OTA-Slot-Layout) oder per OTA/Web-Upload aktualisieren. **Nie** `idf.py flash` bzw. einen Full-Flash/`erase-flash` auf einem eingerichteten Panel: das überschreibt Bootloader, Partitionstabelle und otadata und löscht mit `erase-flash` auch NVS (WLAN, Miniserver, alle Einstellungen).
- Der `flash monitor`-Befehl oben ist nur für die Erstinbetriebnahme eines leeren Boards gedacht.

```sh
# Bootmodus von Hand (IO0 halten, RESET), dann:
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee -p <PORT> app-flash
```

## Release und OTA

- `ci.yml` baut Panlee bei jedem Push mit Wegwerf-Schlüssel, `test-image.yml` hat die Targets `panlee` und `all`, `release.yml` hängt `loxone_panel_panlee.bin` (signiert, mit `.sha256`) an jedes Release.
- „Firmware prüfen“ mit der Standard-URL holt das neueste Release über die GitHub-API und nimmt das Asset `loxone_panel_panlee.bin` (`ota_board_asset()` aus dem Projektnamen). Releases, die vor dieser Änderung entstanden sind, haben kein Panlee-Asset: Dort meldet das Panel „Kein Update für dieses Board“.
- Alternativ: signiertes Test-Image (`target=panlee`) per Upload in der Web-Wartung.
- Die Board-Prüfung lehnt Guition- oder D1-Images auf dem Panlee ab und umgekehrt.
