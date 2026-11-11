# esp_display – allgemeine Entwicklerdoku (alle Geräte)

Native ESP-IDF-Firmware für 480×480-Touchpanels mit ESP32-S3. Kein Arduino-Core auf dem ESP32 (siehe `.cursor/rules/esp32s3-idf.mdc`).

- ESP-IDF **6.1** (`main/idf_component.yml`: `idf >=6.1,<7`)
- LVGL **8.4** (`lvgl/lvgl ~8.4.0`, Component Registry, fixiert in `dependencies.lock`), dazu `espressif/esp_jpeg`
- ArduinoJson als lokale Komponente unter `components/ArduinoJson`
- Version aus `version.txt` (ESP-IDF übernimmt die Datei als `PROJECT_VER`), aktuell `0.9.10`

Gerätespezifisches steht in [panlee.md](panlee.md), [guition.md](guition.md) und [sensecap_d1.md](sensecap_d1.md).

---

## 1. Repo-Aufbau

| Pfad | Inhalt |
|---|---|
| `CMakeLists.txt` | Projekt, Auswahl des Boards über `-DPANEL_BOARD`, Projektname = Board-ID |
| `main/` | gesamte Firmware (C++17), Schriften als `font_*.c`, UI-Teile als `panel_ui_*.inc` |
| `main/Kconfig.projbuild` | Projektoptionen (Menü „Loxone panel“) |
| `main/board_pins*.h` | Pins und Fähigkeiten je Board (`kBoardName`, `kBoardHasRelay`, …) |
| `main/panel_hw.cpp`, `panel_hw_panlee.cpp`, `panel_hw_sensecap_d1.cpp` | Display-, Backlight- und Touch-Treiber je Board (genau einer wird gebaut) |
| `components/ArduinoJson` | JSON-Parser |
| `rp2040/` | RP2040-Firmware für den SenseCAP D1 (Arduino, GPL-3.0) und die gemeinsame Bibliothek `RpScope` |
| `partitions.csv` | 16-MB-Layout (Guition, Panlee) |
| `partitions_sensecap.csv` | 8-MB-Layout (SenseCAP D1) |
| `sdkconfig.defaults` | Basis-Konfiguration ohne Geheimnisse |
| `sdkconfig.defaults.panlee`, `sdkconfig.defaults.sensecap` | werden je Board angehängt |
| `sdkconfig.test` | Test-Firmware mit seriellem Harness |
| `sdkconfig.defaults.local.example` | Vorlage für lokale Geheimnisse |
| `tools/` | Host-Unit-Tests (`test_*.cpp`), Panel-Tests über den Harness (`*_test.py`), `check_secrets.py`, `tag_release.ps1`, öffentlicher Signaturschlüssel `esp_display_signing_pub.pem` |
| `.github/workflows/` | `ci.yml`, `test-image.yml`, `release.yml` |
| `docs/` | diese Doku, Screenshots, ältere Architekturnotiz |

Wichtige Module in `main/`:

| Datei(en) | Aufgabe |
|---|---|
| `app_main.cpp`, `main.cpp` | Start (NVS, netif, Event-Loop), Tasks, UI-Loop, Netz- und Loxone-Ablauf |
| `panel_ui.cpp` + `panel_ui_*.inc` | alle Screens (eine Übersetzungseinheit): Home, Einstellungen, Wetter, Info, Kameras, Energie, Co4, Erinnerungen, Playground, Oszi |
| `net.cpp` | WLAN (Scan, Verbinden, Pause, 802.11k/v) |
| `info_net.cpp`, `transit_parse.cpp`, `radar*.cpp`, `weather_derive.cpp` | Wetter, MeteoSchweiz, ÖV, Regenradar |
| `loxone_client.cpp`, `loxone_structure.cpp`, `loxone_cache.cpp`, `loxone_auth.h` | Miniserver (HTTP, Basic Auth), `LoxAPP3.json`, Struktur-Cache in NVS |
| `webcams.cpp`, `ical.cpp`, `waste_api.cpp`, `waste_window.cpp` | Windy-Webcams, iCal, Abfallkalender |
| `energy*.cpp`, `pv*.cpp` | whatwatt Go (Strom/Wasser), PV über Modbus-TCP-Gateway (nur lesen) |
| `co4_*.cpp` | Co4 Energie: OIDC Device Flow, BFF-Abfragen, eigener Task |
| `co4en*.cpp`, `espnow_play.cpp` | CO4EN-Sender über ESP-NOW (Playground) |
| `settings.cpp`, `settings_schema.cpp` | NVS-Einstellungen, Schema, Backup/Restore |
| `maint.cpp`, `ota_check.cpp`, `ota_board.cpp`, `ota_ver.h` | Web-Wartung, OTA, Board- und Versionsprüfung |
| `clock_sync.cpp` | SNTP und Zeitzone |
| `diag.cpp`, `test_harness.cpp` | Diagnose-Konsole und Test-Harness |
| `rp_link.cpp`, `scope_view.cpp` | Oszi/Logik (nur SenseCAP D1) |

---

## 2. Architektur

### 2.1 Tasks und Core-Aufteilung

Grundregel: Netzwerk und Protokolle auf **Core 0**, UI auf **Core 1**. Nur der `ui`-Task ruft LVGL auf. Alle Tasks werden mit `xTaskCreatePinnedToCore(…WithCaps)` erzeugt.

| Task | Core | Prio | Stack | Datei | Aufgabe |
|---|---|---|---|---|---|
| `ui` | 1 | 5 | 16 KB intern | `main.cpp` | LVGL, Touch, Harness, `poll()` (Ablaufsteuerung) |
| `lox` | 0 | 5 | 8 KB PSRAM | `main.cpp` | Loxone-Struktur, Befehle, Statusabfragen (eigene Queue, eine Anfrage gleichzeitig) |
| `net` | 0 | 4 | 16 KB intern | `main.cpp` | WLAN-Steuerung, Wetter, MeteoSchweiz, ÖV, Kalender/Abfall, Webcams, OTA, Einstellungen mit Netzbezug |
| `co4` | 0 | 3 | 10 KB PSRAM | `co4_task.cpp` | Co4-Anmeldung und Datenabruf |
| `energy` | 0 | 3 | 10 KB PSRAM | `energy_task.cpp` | whatwatt-Go-Abfrage |
| `pv` | 0 | 2 | 6 KB PSRAM | `pv_task.cpp` | PV über Modbus TCP |
| `radar` | 0 | 2 | 12 KB PSRAM | `radar.cpp` | Regenradar-Kacheln (nur bei Bedarf) |
| `co4en` | 0 | 2 | 8 KB PSRAM | `espnow_play.cpp` | CO4EN/ESP-NOW (erst nach Benutzung des Playgrounds) |
| `diag` | 0 | 2 | 6 KB | `diag.cpp` | Statistik, Diagnose-Konsole |
| `rplink` | 0 | 1 | 6 KB PSRAM | `rp_link.cpp` | UART zum RP2040 (nur D1, erst beim ersten Öffnen des Oszi) |
| `restart` | 0 | 1 | 2 KB | `maint.cpp` | verzögerter Neustart nach Restore/Upload |
| `httpd` | 0 | IDF | IDF | `maint.cpp` | Wartungs-Webserver (nur wenn eingeschaltet) |

Der `ui`-Task schläft bis zum nächsten fälligen LVGL-Timer, höchstens 10 ms, oder bis ein anderer Task ihn per `xTaskNotifyGive` weckt.

### 2.2 UI-Queue und Datenfluss

```
 UI-Task (Core 1)                          Core 0
 ┌──────────────┐   net_q (NetRequest)   ┌──────────┐
 │ poll()       │ ─────────────────────▶ │ net_task │──┐
 │ handle_result│ ◀───────────────────── │          │  │ res_q (NetResult)
 │              │   lox_q                └──────────┘  │ + xTaskNotifyGive(ui)
 │              │ ─────────────────────▶ ┌──────────┐  │
 │              │ ◀───────────────────── │ lox_task │──┘
 └──────────────┘                        └──────────┘
```

- Die UI hat höchstens eine Anfrage bei `net` und eine bei `lox` offen (`net_busy`, `lox_busy`).
- Grosse Ergebnisse (Loxone-Steuerungen, Wetterwoche, Kalender, Fahrplan) liegen in statischem Speicher. Der Netz-Task schreibt vor dem Senden, die UI liest nach dem Empfang. Die Queue ordnet die Zugriffe.
- Loxone hat einen eigenen Task, damit ein OTA-Download, ein Kamerabild oder ein WLAN-Wechsel auf `net` keinen Lichtschalter verzögert.
- Die neueren Tasks (`co4`, `energy`, `pv`, `rplink`) haben eigene Queues bzw. Snapshots. Die UI holt ihre Ergebnisse in eigenen LVGL-Timern ab.
- Flags zwischen Tasks sind `std::atomic`.

### 2.3 Ablauf beim Start (`main.cpp`)

1. `app_main`: NVS initialisieren (bei Formatwechsel löschen und neu anlegen), `esp_netif`, Event-Loop, dann `app_start()`.
2. Tasks starten, im `ui`-Task: Konsole, Relais (falls vorhanden), Display, Touch, LVGL, `ui_init()`.
3. **WLAN und Loxone sind optional.** Das Panel startet immer auf die Startseite (`phase = Ready`).
   - Mit gespeichertem WLAN verbindet es im Hintergrund weiter. Ohne WLAN läuft die Station nur für die Netzsuche.
   - Ohne Miniserver-Konfiguration öffnet sich beim allerersten Start einmal das Miniserver-Formular (mit „Überspringen“, gespeichert als `lox_skip`).
   - Liegt eine Loxone-Struktur im NVS-Cache, werden die Kacheln sofort angezeigt (offline markiert).
4. Sobald WLAN steht: SNTP, Laden der Netzinhalte, `maint_mark_valid()` (OTA-Bestätigung), Loxone-Struktur laden.
5. Fehlgeschlagene Strukturladungen wiederholt `lox_backoff_ms()` nach 10 s, 30 s, 2 min und dann alle 5 min (`retry_logic.h`).

Zeitkonstanten WLAN (`main.cpp`): `kWifiTimeoutMs = 30000`, `kWifiSwitchMs = 20000` (Netzwechsel in den Einstellungen: danach zurück zum bisherigen Netz), je 3 Auth- bzw. „AP nicht gefunden“-Fehler. „Ohne Netzwerk starten“ pausiert WLAN nur für die laufende Sitzung (nicht in NVS).

### 2.4 Speicher

- PSRAM octal 80 MHz, `SPIRAM_USE_MALLOC`. `malloc` unter 2 KB bleibt intern, 64 KB intern reserviert.
- `SPIRAM_XIP_FROM_PSRAM=y`: Code/Rodata aus PSRAM, damit Flash-Zugriffe (NVS, WLAN-Kalibrierung) die RGB-Ausgabe nicht stören.
- Grosse statische Objekte mit `EXT_RAM_BSS_ATTR` in PSRAM.
- LVGL-Heap über `main/lv_mem_psram.h` vollständig in PSRAM.
- mbedTLS-Kontexte in PSRAM (`MBEDTLS_EXTERNAL_MEM_ALLOC`).
- Watchdogs: Task-WDT 5 s mit Panic, Interrupt-WDT 800 ms. Core-Dump in die Partition `coredump` (`idf.py coredump-info`).

---

## 3. Build-System und Targets

| `PANEL_BOARD` | Kconfig-Wahl | Projektname = Board-ID | Zusatz-Defaults | Partitionen | Treiber |
|---|---|---|---|---|---|
| `guition` (Standard) | `PANEL_BOARD_GUITION` | `loxone_panel` | – | `partitions.csv` | `panel_hw.cpp` |
| `panlee` | `PANEL_BOARD_PANLEE` | `loxone_panel_panlee` | `sdkconfig.defaults.panlee` | `partitions.csv` | `panel_hw_panlee.cpp` |
| `sensecap_d1` | `PANEL_BOARD_SENSECAP_D1` | `loxone_panel_sensecap_d1` | `sdkconfig.defaults.sensecap` | `partitions_sensecap.csv` | `panel_hw_sensecap_d1.cpp` |

`main/CMakeLists.txt` bricht ab, wenn Kconfig-Wahl und Projektname nicht zusammenpassen. Deshalb braucht jedes Board ein **eigenes Build-Verzeichnis** und eine eigene `sdkconfig`:

```sh
. ~/esp/esp-idf/export.sh            # Windows: . $HOME\esp\v6.1\esp-idf\export.ps1

# Guition (Ausgabe build/loxone_panel.bin)
idf.py set-target esp32s3            # einmalig
idf.py build

# Panlee (Ausgabe build-panlee/loxone_panel_panlee.bin)
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee set-target esp32s3
idf.py -B build-panlee -DSDKCONFIG=build-panlee/sdkconfig -DPANEL_BOARD=panlee build

# SenseCAP D1 (Ausgabe build-d1/loxone_panel_sensecap_d1.bin)
idf.py -B build-d1 -DSDKCONFIG=build-d1/sdkconfig -DPANEL_BOARD=sensecap_d1 set-target esp32s3
idf.py -B build-d1 -DSDKCONFIG=build-d1/sdkconfig -DPANEL_BOARD=sensecap_d1 build

# Flashen: Port immer explizit angeben
idf.py -B <dir> -DSDKCONFIG=<dir>/sdkconfig [-DPANEL_BOARD=…] -p <PORT> flash monitor
```

Reihenfolge der Defaults: `sdkconfig.defaults` → Board-Datei → (bei Test-Builds `sdkconfig.test`) → `sdkconfig.defaults.local`, falls vorhanden. Die generierte `sdkconfig` nie von Hand ändern, nach Kconfig-Änderungen `idf.py reconfigure` oder Build-Verzeichnis löschen.

Test-Firmware mit seriellem Harness (460800 Baud):

```sh
idf.py -B build-test -DSDKCONFIG=build-test/sdkconfig -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test" build
```

### 3.1 Partitionstabellen

| Partition | 16 MB (`partitions.csv`) | 8 MB (`partitions_sensecap.csv`) |
|---|---|---|
| nvs | 0x9000, 0x6000 | 0x9000, 0x6000 |
| otadata | 0xF000, 0x2000 | 0xF000, 0x2000 |
| phy_init | 0x11000, 0x1000 | 0x11000, 0x1000 |
| ota_0 | 0x20000, 0x600000 (6 MB) | 0x20000, 0x360000 (3,375 MB) |
| ota_1 | 0x620000, 0x600000 | 0x380000, 0x360000 |
| coredump | 0xC20000, 0x40000 | 0x6E0000, 0x40000 |

NVS bleibt bei 0x9000, damit Einstellungen den Wechsel vom alten Factory-Layout überleben.

### 3.2 Kconfig-Optionen (`main/Kconfig.projbuild`)

| Option | Zweck |
|---|---|
| `PANEL_BOARD_*` | Board-Wahl (wird von der Board-Defaults-Datei gesetzt) |
| `PANEL_TEST_HARNESS` | serieller Test-Harness (`SHOT`, `TAP`, `SWIPE`, `STATE` …) |
| `PANEL_DIAG_INJECT` | erlaubt auch im Release Touch-Injektion und Laufzeit-Overrides über die Konsole |
| `PANEL_SERIAL_BAUD` | Konsolen-Baudrate nach dem Boot (115200, mit Harness 460800) |
| `PANEL_WINDY_API_KEY`, `PANEL_ICAL_URL` | Build-Defaults für Geheimnisse, **nur** über `sdkconfig.defaults.local` setzen |
| `PANEL_WASTE_API`, `PANEL_WASTE_BFS` | Abfallkalender-API und Gemeinde |
| `PANEL_OTA_URL` | Standard-Update-URL (GitHub „latest release“) |
| `PANEL_METEO_PLZ` | MeteoSchweiz-PLZ, 4 Ziffern (Standard `9470`; ein alter Wert `947000` wird umgewandelt) |
| `CO4EN_LINK_MISS_MAX`, `CO4EN_RESWEEP_MIN_S`, `CO4EN_RESWEEP_MAX_S` | CO4EN-Gateway-Suche |
| `PANEL_CO4_REALM_URL`, `PANEL_CO4_CLIENT_ID`, `PANEL_CO4_SCOPE`, `PANEL_CO4_BFF_URL` | Co4: öffentlicher Keycloak-Client (Device Authorization Grant, ohne Client-Secret) und BFF |

---

## 4. Konfiguration und NVS

Alle Einstellungen liegen in NVS, Namespace `panel`.

- **Schema:** `main/settings_schema.cpp` beschreibt jeden Schlüssel mit Typ, Grenzen und Flags (`kKeySecret`, `kKeyUrl`, `kKeyHttpsOnly`, `kKeyHost`, `kKeyDigits`, `kKeyFloat`, `kKeyInternal`). Host-Test: `tools/test_settings_schema.cpp`.
- **Ein Schreibpfad:** `Tx` in `settings.cpp` prüft alle Werte gegen das Schema, schreibt und committet unter einem Mutex. Restore, Migration und Build-Seed nutzen denselben Mutex.
- **Migration:** `cfg_ver` und `settings_migrate()`.
- **Interne Schlüssel** (`kKeyInternal`, z. B. `co4_rt` = Co4-Refresh-Token, Verläufe `en_hist`, `pv_hist`, `pv_hours`) werden nie exportiert.
- **Geheimnisse** (`wifi_pass`, `lox_pass`, `windy_key`, `windy_dflt`, `ical_url`, `ical_dflt`) werden am Display maskiert, nie geloggt und nur nach Bestätigung am Panel exportiert.
- Weitere Gruppen: Ort (`loc_*`), NTP/Zeitzone, Kameras (`cams_n`, `cam_ids`, max. 12), ÖV (`st0_*`, `st1_*`), Bildschirm (`scr_to`, `scr_hint`, `scr_saver`, `ui_design`), Kalender/Abfall (`ical_url`, `waste_api`, `waste_bfs`, `waste_zone`), OTA (`ota_url`, nur https), MeteoSchweiz (`meteo_plz`), Erinnerungen (`rem_*`, `cal_*`, `wst_*`), Loxone (`lox_*`), Energie (`en_*`, `wa_*`), PV (`pv_*`), Schalter-Taste (`sw_target`), Co4 (`co4_zone`, `co4_zname`, `co4_site`).
- Der Struktur-Cache der Loxone-Steuerungen liegt ebenfalls in NVS (`loxone_cache.cpp`, max. 4 KB).
- **MeteoSchweiz-PLZ** (`meteo_plz`, `kTextPlz`): gespeichert wird die normale 4-stellige PLZ (`9470`). Eingabe am Gerät unter Orte & Infos › MeteoSchweiz-PLZ (Zahlentastatur, max. 4 Zeichen, Prüfung 1000–9999 mit `meteo_plz_valid()`). `settings_load_text()` normalisiert jeden Wert mit `meteo_plz_normalize()`: alte 6-stellige Werte mit Endung `00` aus NVS, Restore oder `sdkconfig` (`947000`) werden zu `9470`, Ungültiges fällt auf den Build-Default zurück. `settings_save_text()` speichert nur 4 Ziffern. Das Schema erlaubt 4–6 Ziffern, damit alte Sicherungen weiter importiert werden. Für die Anfrage an `app-prod-ws.meteoswiss-app.ch/v1/plzDetail` hängt `meteo_plz_query()` `00` an. Code: `main/meteo_plz.cpp`, Host-Test `tools/test_meteo_plz.cpp`.

---

## 5. Geheimnisse und `check_secrets.py`

**Grundsatz:** Release- und Test-Images enthalten keine Geheimnisse. Das Panel hält seine Zugangsdaten nur in NVS.

| Datei | Im Git | Inhalt |
|---|---|---|
| `sdkconfig.defaults.local` | nein (gitignored) | Build-Defaults für Windy-Key und iCal-URL, wird automatisch angehängt |
| `main/panel_config.h` | nein (gitignored) | WLAN- und Miniserver-Zugang für lokale Builds (Vorlage `main/panel_config.example.h`) |
| `secure/signing_key.pem` | nein (gitignored, `*.pem` ausser dem öffentlichen Schlüssel) | privater Signaturschlüssel |
| `sensecap_backup/`, `*_backup*.bin` | nein | Flash-Backups (enthalten NVS mit WLAN-Daten) |

Hinweis: Der Kommentar in `panel_config.example.h` nennt `include/panel_config.h`. Gefunden wird die Datei aber über `main/` (`config_load.h` mit `__has_include`, Include-Pfad `main`). `include/panel_config.h` ist ebenfalls gitignored und wird von CI geprüft.

**Build-Seed:** Beim ersten Start einer lokal gebauten Firmware kopiert `settings_seed_from_build()` WLAN und Loxone nur dann nach NVS, wenn dort noch nichts steht. Das WLAN wird genau einmal übernommen (`wifi_seeded`). Windy-Key und iCal-URL landen als separate Default-Kopien (`windy_dflt`, `ical_dflt`).

**`tools/check_secrets.py`** gibt nie Werte aus, nur Namen und Längen:

```sh
# Lokal: sucht die Werte aus sdkconfig.defaults.local und panel_config.h im Image
python tools/check_secrets.py build/loxone_panel.bin        # erwartet: 0 secret value(s) found

# CI-Modus (sauberer Checkout): sucht geheimnisartige Muster
python tools/check_secrets.py --generic build/loxone_panel.bin
```

Der generische Modus sucht private iCal-Tokens, private Google-Kalender-URLs, JWTs (OIDC-Tokens), `client_secret=` und prüft, dass in der zugehörigen `sdkconfig` kein Windy-Key, keine iCal-URL und keine WLAN/Loxone-Zugangsdaten gesetzt sind. Exit-Code 1 bei Fund.

Zusätzlich prüfen Release- und Test-Image-Workflow, dass `sdkconfig.defaults.local`, `main/panel_config.h` und `include/panel_config.h` nicht im Checkout liegen und dass `CONFIG_PANEL_WINDY_API_KEY` und `CONFIG_PANEL_ICAL_URL` leer sind.

---

## 6. Signatur und OTA

### 6.1 Signierte Apps

`sdkconfig.defaults`: signierte Apps **ohne** Hardware-Secure-Boot (keine eFuses):

- `SECURE_SIGNED_APPS_NO_SECURE_BOOT`, RSA-3072 nach Secure-Boot-v2-Schema, `SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT`, `SECURE_BOOT_BUILD_SIGNED_BINARIES`.
- Der Build signiert mit `secure/signing_key.pem`. Ohne Schlüssel bricht der Build ab, auch lokal.
- Jedes OTA-Image (URL oder Upload) muss mit demselben Schlüssel signiert sein wie die laufende App. Die laufende App prüft mit dem öffentlichen Schlüssel aus ihrem eigenen Signaturblock.
- Für reine Compile-Tests reicht ein Wegwerf-Schlüssel: `espsecure generate-signing-key --version 2 --scheme rsa3072 secure/signing_key.pem`. Panels mit offizieller Firmware lehnen so signierte Images ab.
- Prüfen: `espsecure verify-signature --version 2 --keyfile tools/esp_display_signing_pub.pem <bin>`.
- In CI kommt der echte Schlüssel aus dem Repository-Secret `APP_SIGNING_KEY` und wird nach dem Build gelöscht.

### 6.2 Update-Wege

1. **Online** (Einstellungen › System › Wartung & Update › „Firmware prüfen“):
   - Ist die Update-URL eine GitHub-URL der Form `https://github.com/<owner>/<repo>/releases/...`, fragt `ota_check.cpp` `api.github.com/repos/<owner>/<repo>/releases/latest` ab und sucht das Asset `<projektname>.bin` des laufenden Boards (`ota_board_asset`). Fehlt es, meldet das Panel „Kein Update für dieses Board“ statt etwas zu installieren.
   - Bei einer eigenen URL liest es per Range-Request die ersten 256 Bytes, prüft Magic, Board-ID und Version.
   - Angeboten wird nur eine höhere Version (`ota_ver::offer_cmp`). Ein Test-Build `X-test.<sha>` gilt als älter als Release `X`, ein späteres Release wird also wieder angeboten.
   - Installation mit `esp_https_ota` (Zertifikats-Bundle). `ota_url` muss laut Schema mit `https://` beginnen.
2. **Upload** über die Web-Wartung (`POST /update`, siehe Abschnitt 8).

### 6.3 Board-Prüfung

Die Board-ID ist der CMake-Projektname in `esp_app_desc_t.project_name`. `ota_board_check_image()` liest ihn aus den ersten 112 Bytes des Images. Ein Image für ein anderes Board wird vor dem Umschalten der Boot-Partition abgelehnt („Firmware für anderes Board“). Host-Test: `tools/test_ota_board.cpp`.

### 6.4 Rollback

`BOOTLOADER_APP_ROLLBACK_ENABLE=y`. Ein neues Image startet als `PENDING_VERIFY` und markiert sich gültig, sobald WLAN steht (`maint_mark_valid`). Ohne WLAN innerhalb von 10 min (Test-Harness: 90 s) oder bei einem Absturz davor rollt der Bootloader auf das vorherige Image zurück.

---

## 7. CI, Test-Image und Release

### 7.1 `ci.yml` (jeder Push, jeder PR)

- **host-tests:** Mojibake-Prüfung aller Textdateien (ausser Schriften), danach alle Host-Tests mit `g++ -std=c++17` (iCal, OTA-Version/-Board, UI-Text, Retry, Loxone-Cache, MeteoSchweiz-PLZ, Settings-Schema, Abfall, Energie, CO4EN, Co4, PV, Wetter, Schalter-Ziel, ÖV, Radar, Karte, Scope inkl. `rp_scope.c`).
- **build:** im Container `espressif/idf:v6.1` mit **Wegwerf-Schlüssel**: Guition, SenseCAP D1 und Panlee, danach `check_secrets.py --generic` für alle drei Images.

### 7.2 `test-image.yml` (signiertes Test-Image)

- Auslöser: jeder Push auf `master` (ohne reine `.md`-Änderungen, alle drei Boards = `all`) oder manuell über Actions › „Test image“ › „Run workflow“ mit `ref` (Standard `master`) und `target`: `sensecap_d1` (Standard), `guition`, `panlee`, `both` (Guition + SenseCAP D1, Bedeutung unverändert) oder `all` (alle drei).
- **Wichtig:** GitHub nimmt für „Run workflow“ die Workflow-Datei des Branches, der im Dialog unter „Use workflow from“ gewählt ist (Standard: Default-Branch). Die Auswahl `panlee`/`all` erscheint nur, wenn dieser Branch die neue `test-image.yml` enthält. `ref` bestimmt nur, welcher Code gebaut wird.
- Ablauf wie beim Release: sauberer Checkout, Prüfung auf lokale Geheimnisdateien, Version wird zu `<version.txt>-test.<7-stelliger SHA>`, Signatur mit `APP_SIGNING_KEY`, `espsecure verify-signature`, `check_secrets.py --generic`, Prüfung der leeren Geheimnis-Optionen.
- Artefakt `test-image-<target>-<version>`, 7 Tage aufbewahrt: nur `loxone_panel.bin`, `loxone_panel_sensecap_d1.bin` und/oder `loxone_panel_panlee.bin`, `sha256.txt`, `info.txt` (Ref, Commit, Version, Board, Slot-Offsets). Kein Bootloader, keine Partitionstabelle. Andere Dateien lassen den Job scheitern.
- Gedacht für den Upload über die Web-Wartung. Es entsteht weder Tag noch Release.
- Ein neuerer Push auf `master` bricht einen älteren Push-Build ab, manuelle Läufe haben eigene Gruppen je Ref/Target.

### 7.3 `release.yml` (Tag `v*`)

1. `version.txt` erhöhen und committen.
2. Tag setzen, am sichersten mit `tools/tag_release.ps1 <version>`: prüft, dass `HEAD` auf `origin/<branch>` gepusht ist, dass `version.txt` im getaggten Commit passt und der Tag noch nicht existiert. Dann taggt und pusht es.
3. Der Workflow prüft, dass Tag und `version.txt` übereinstimmen, dass keine lokalen Geheimnisdateien vorhanden sind und dass `APP_SIGNING_KEY` gesetzt ist.
4. Build Guition, SenseCAP D1 und Panlee, Signaturprüfung, `check_secrets.py --generic`, Prüfung der leeren Geheimnis-Optionen, Schlüssel löschen.
5. GitHub-Release mit `loxone_panel.bin`, `loxone_panel_sensecap_d1.bin`, `loxone_panel_panlee.bin` und je einer `.sha256`, Release-Notes automatisch. Die erste sinnvolle Zeile der Notes zeigt das Panel als „Neu: …“ an.

Releases entstehen nur über einen `v*`-Tag. Das Panel sucht im neuesten Release das Asset `<project_name>.bin` (`ota_board_asset()`), also `loxone_panel.bin`, `loxone_panel_sensecap_d1.bin` bzw. `loxone_panel_panlee.bin`. Releases vor diesem Stand enthalten kein Panlee-Asset.

---

## 8. Web-Wartungs-API (`main/maint.cpp`)

- HTTP-Server auf Port 80, Core 0. **Standardmässig aus**, auch nach jedem Neustart. Einschalten am Panel unter Wartung › „Web-Wartung“.
- Bei jedem Start wird eine neue 6-stellige PIN erzeugt (`esp_random`), die nur am Panel angezeigt wird (`http://<IP>/  PIN <pin>`).
- Jeder Endpunkt ausser `/` verlangt den Header `X-Panel-Pin`. Der eigene Header erzwingt bei Browsern einen CORS-Preflight, den der Server nie erlaubt. Damit können fremde Webseiten keine Anfragen an das Panel schicken (CSRF-Schutz). Der PIN-Vergleich läuft in konstanter Zeit.
- 5 falsche PINs sperren die API für 60 s.
- Der Server schaltet sich 15 min nach der letzten authentisierten Anfrage selbst aus (Harness kann das mit `WEBIDLE` verkürzen).

| Methode | Pfad | Funktion |
|---|---|---|
| GET | `/` | HTML-Seite (Status, PIN-Feld, Backup, Restore, Upload). Ohne PIN |
| GET | `/status` | JSON: `version`, `built`, `idf`, `partition`, `uptime_s`, `wifi`, `ssid`, `rssi`, `ip`, `heap_internal`, `heap_internal_largest`, `heap_psram`, `ntp_synced`, `ntp_age_s` |
| GET | `/backup` | Einstellungen als JSON, ohne Geheimnisse |
| GET | `/backup?secrets=1` | mit Geheimnissen: Der erste Aufruf löst am Panel eine Rückfrage aus („Backup mit Geheimnissen?“). Nach „Erlauben“ ist **ein** Download innerhalb von 60 s möglich |
| POST | `/restore` | JSON-Body (max. 16 KB), Prüfung gegen das Schema, Antwort mit Anzahl und ignorierten Schlüsseln, danach Neustart |
| POST | `/update` | Roh-Body = signierte App-`.bin`. Schreibt sequenziell in die nächste OTA-Partition, prüft die Board-ID schon am Header, prüft die Signatur am Ende. Antwort „Firmware installiert. Neustart …“ oder ein Fehlertext |

Beispiel Upload (PIN nicht in Skripte oder Logs schreiben):

```sh
sha256sum loxone_panel_sensecap_d1.bin      # mit sha256.txt vergleichen
curl -X POST -H "X-Panel-Pin: <PIN>" --data-binary @loxone_panel_sensecap_d1.bin http://<IP>/update
```

Nach dem Neustart ist die Web-Wartung wieder aus. Die neue Version liest man am Panel unter Wartung ab oder nach erneutem Einschalten über `/status`.

---

## 9. Diagnose und Tests

- **Konsole** (UART0, 115200 Baud), auch im Release, nur lesend: `DIAGHEALTH`, `DIAGENERGY`, `DIAGSTATS`, `DIAGSTACK`, `DIAGHW`, `DIAGFB`, `DIAGGT`, `DIAGCO4`, `DIAGCO4EN`, `DIAGTIME`, `SCANWIFI`. Nur mit `CONFIG_PANEL_DIAG_INJECT` oder Harness: `DIAGTAP`, `DIAGSWIPE`, `DIAGRAW`, `DIAGVER`, `DIAGSCRTO`, `DIAGSAVER`, `DIAGBL`.
- **Test-Harness** (`sdkconfig.test`, 460800 Baud): `SHOT`, `TAP`, `SWIPE`, `STATE`, `POS`, `LIST`, `TYPE`, `SCRTO`, `SCROLLCHK`, `REBOOT` u. a., genutzt von `tools/*_test.py`.
- `tools/ui_pages_test.py` ist in jedem UI-Testlauf Pflicht: jede Seite bis unten scrollen, letztes Element sichtbar und antippbar.
- Host-Tests: `tools/test_*.cpp`, Aufrufe siehe `ci.yml`.
- Die Tool-Übersicht steht im Haupt-[README](../../README.md#test-harness--tools).

---

## 10. Konventionen

- Kein Arduino-API und keine Legacy-IDF-Treiber auf dem ESP32 (`i2c_master`, `esp_lcd`, `gptimer` …).
- Tasks immer gepinnt, LVGL nur im UI-Task, Datenaustausch über Queues/Notifications.
- Texte auf dem Panel sind Deutsch. Dateien als UTF-8 speichern, CI prüft auf Mojibake.
- Nie Geheimnisse committen oder loggen. Vor jedem Release-Upload `check_secrets.py` laufen lassen.
- Flashen immer mit explizitem `-p <PORT>`.
