# Guition-Panel (ESP32-4848S040C) – Geräteanleitung

4-Zoll-Touchpanel (480×480) für die 86-mm-Unterputzdose, mit eingebautem Relais. Bedienung und Einstellungen stehen in [allgemein.md](allgemein.md).

## Anschluss und Stromversorgung

- Versorgung: **5 V**.
- Die USB-C-Buchse ist für Service und Tests gedacht.
- Nach dem Einschalten erscheint nach wenigen Sekunden die Startseite.

## Montage

- Das Panel passt in eine 86-mm-Unterputzdose.
- **Netzspannung (230 V):** Wird das Panel mit einem Sockel bzw. einer Frontplatte mit 230-V-Anschluss eingebaut, darf die Installation nur eine Elektrofachperson mit Bewilligung nach der Niederspannungs-Installationsverordnung (**NIV**) ausführen. Sockel und Frontplatte sind nicht zertifiziert.
- Vor Arbeiten an der Dose immer die Sicherung ausschalten.

## Besonderheit: eingebautes Relais

- Das Relais erscheint unter **Loxone › Schalter** als eigene Kachel. Es funktioniert auch ohne Miniserver.
- Es kann auch der Taste **Schalter** auf der Startseite zugewiesen werden (Einstellungen › Orte & Infos › Schalter-Taste).
- Nach jedem Neustart oder Stromausfall ist das Relais **aus**.
- Die zulässige Last des Relaiskontakts ist hier nicht dokumentiert (**offen**). Schalte damit keine Netzlasten, ohne die Angaben des Herstellers geprüft zu haben, und lass Netzlasten nur von einer Elektrofachperson anschliessen.

## Updates

Das Guition-Panel erhält Updates über **Firmware prüfen** (siehe [allgemein.md](allgemein.md#7-updates)).
