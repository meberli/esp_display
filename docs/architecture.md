# Architektur esp_display (ESP32-4848S040C, ESP-IDF 6.1, LVGL 8.4)

## Tasks

| Task | Core | Prio | Stack | Aufgabe |
|---|---|---|---|---|
| `ui` | 1 | 5 | 16 KB intern | LVGL, Touch, Test-Harness. Einzige Task, die LVGL aufruft. Schläft bis zum nächsten LVGL-Timer (max. 10 ms) oder einer Notification. |
| `lox` | 0 | 5 | 8 KB PSRAM | Loxone-Befehle und Status-Abfragen (eigene Queue, eine Anfrage gleichzeitig). |
| `net` | 0 | 4 | 12 KB intern | WLAN-Steuerung, LoxAPP3-Struktur, Wetter, ÖV, Kalender/Abfall, MeteoSchweiz, Webcams, OTA, Einstellungen mit Netz (Scan, Geocode ...). |
| `httpd` | 0 | (IDF) | 5 KB | Wartungs-Webseite (nur eingeschaltet, PIN, 15 min Auto-Aus). |
| `diag` | 0 | 2 | 4 KB | Statistik (DIAG-Zeilen, Stack/Heap), Release-Konsole (DIAG-Befehle, ohne `CONFIG_PANEL_DIAG_INJECT` nur lesend). |

## Datenfluss

```
 UI-Task (core 1)                          core 0
 ┌──────────────┐   net_q (NetRequest)   ┌──────────┐
 │ poll()       │ ─────────────────────▶ │ net_task │──┐
 │ handle_result│ ◀───────────────────── │          │  │  res_q (NetResult)
 │              │   lox_q                └──────────┘  │  + xTaskNotifyGive(ui)
 │              │ ─────────────────────▶ ┌──────────┐  │
 │              │ ◀───────────────────── │ lox_task │──┘
 └──────────────┘                        └──────────┘
```

* Die UI stellt je eine Anfrage an `net` und `lox` (Flags `net_busy`, `lox_busy`).
* Grosse Ergebnisse (Steuerungen, Wetterwoche, Kalender, Fahrplan) liegen in
  statischem Speicher: die Netz-Task schreibt vor dem Senden, die UI liest nach
  dem Empfang (die Queue ordnet die Zugriffe, eine Anfrage in Arbeit).
* Flags zwischen Tasks sind `std::atomic` (z. B. OTA-Fortschritt, Neustart).

## Einstellungen (NVS, Namespace `panel`)

* Schema: `main/settings_schema.cpp` (Typ, Grenzen, Flags Secret/Url/HttpsOnly ...).
* Ein Schreibpfad: `Tx` in `settings.cpp` prüft alle Werte gegen das Schema,
  schreibt dann alles und committet unter einem Mutex. Restore, Migration und
  Build-Seed nehmen denselben Mutex.
* Backup/Restore über die Wartungs-Webseite (Secrets nur nach Bestätigung am Panel).
* `cfg_ver` + `settings_migrate()` für spätere Formatänderungen.

## Partitionen (16 MB Flash)

| Name | Offset | Grösse |
|---|---|---|
| nvs | 0x9000 | 24 KB |
| otadata | 0xF000 | 8 KB |
| ota_0 | 0x20000 | 6 MB |
| ota_1 | 0x620000 | 6 MB |
| coredump | 0xC20000 | 256 KB |

(siehe `partitions.csv`)

## OTA

1. Prüfen: `ota_check` liest die Release-Info (GitHub, nur https) bzw. den
   Image-Header einer eigenen URL.
2. Installieren: `esp_https_ota` (online) oder Upload über die Wartungs-Webseite.
3. Signatur: signierte Apps ohne Secure Boot (RSA-3072, PSS). Das laufende Image
   prüft neue Images mit dem öffentlichen Schlüssel aus seinem Signaturblock;
   unsignierte oder fremd signierte Images werden abgelehnt. Keine eFuses.
4. Bestätigung: das neue Image startet als PENDING_VERIFY und markiert sich
   gültig, sobald das WLAN steht (`maint_mark_valid`). Ohne WLAN nach 10 min
   (oder bei Absturz vorher) Rollback auf das vorherige Image.

## Build, Tests, CI

* Lokal: `idf.py build flash` (Signierschlüssel in `secure/signing_key.pem`,
  gitignored). Test-Build mit Harness: `sdkconfig.test` (`CONFIG_PANEL_TEST_HARNESS`).
* Host-Tests: `tools/test_*.cpp` (iCal, OTA-Version, Settings-Schema, Wetter).
* Panel-Tests: `tools/*_test.py` über die serielle Harness (COM-Port).
* CI (`.github/workflows/ci.yml`): Host-Tests + Firmware-Build mit Wegwerf-Schlüssel
  + generischer Secret-Scan. Release (`release.yml`) nur bei Tag `v*`, signiert
  mit dem Secret `APP_SIGNING_KEY`, Signatur geprüft, Secret-Scan.

## Abfallkalender

`waste_api.cpp` liest die Abfallkalender-API (`<waste_api>/api/abfallkalender?gemeinde=<waste_bfs>&ab=<heute>`,
Standard `https://abfall-sg.hispeed.workers.dev`, BFS 3271 = Buchs SG). Bei Gemeinden mit mehreren Touren
liefert die API `zonen`; ist `waste_zone` gesetzt und vorhanden, gelten deren Termine, sonst die Vereinigung.
Die Netz-Task hält die Termine in einem eigenen Cache: Abruf beim Start, dann einmal pro Tag (bei Fehlern
höchstens stündlich, bei geänderter Einstellung sofort). Host-Test: `tools/test_waste_api.cpp`.
