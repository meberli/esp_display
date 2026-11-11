# Entwicklerdokumentation esp_display

Diese Doku richtet sich an alle, die die Firmware bauen, ändern, testen oder veröffentlichen. Die Bedienung für Endkunden steht separat unter [`../kunde/`](../kunde/README.md).

Stand: Branch `playground-scope` (Basis `panlee`, Version `0.9.10` aus `version.txt`, plus Oszi/Logik für den SenseCAP D1). Massgebend ist immer der Code. Stellen, die im Code nicht eindeutig sind, sind mit **offen** markiert.

| Datei | Inhalt |
|---|---|
| [allgemein.md](allgemein.md) | Gilt für alle Geräte: Architektur, Tasks und Cores, UI-Queue, Build und Targets, Konfiguration/NVS, Geheimnisse und `check_secrets.py`, Signatur und OTA, CI und Test-Image, Web-Wartungs-API, Release, Repo-Aufbau |
| [panlee.md](panlee.md) | Panlee ZX3D95CE01S-TR-V12 (Target `panlee`) |
| [guition.md](guition.md) | Guition ESP32-4848S040C (Target `guition`, Standard) |
| [sensecap_d1.md](sensecap_d1.md) | Seeed SenseCAP Indicator D1 (Target `sensecap_d1`), inkl. RP2040-Firmware, Scope-Protokoll und Flashen per UF2 |

Weitere technische Dokumente im Repo:

- [`../architecture.md`](../architecture.md): ältere Kurzfassung der Architektur (Tasks, Datenfluss, Partitionen). Bei Widersprüchen gilt diese Doku bzw. der Code.
- [`../co4en_node_v05.md`](../co4en_node_v05.md): CO4EN-Knotenseite (ESP-NOW, Playground).
- [`../../rp2040/README.md`](../../rp2040/README.md): RP2040-Sketch (englisch).
