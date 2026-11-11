# Panlee-Panel (ZX3D95CE01S-TR) – Geräteanleitung

3,95-Zoll-Touchpanel (480×480) für die Unterputzmontage. Bedienung und Einstellungen stehen in [allgemein.md](allgemein.md).

## Anschluss und Stromversorgung

- Versorgung und Anschlussklemmen nach den Angaben des Herstellers (Wireless-Tag/Panlee). Die genaue Versorgungsspannung ist in dieser Anleitung noch nicht festgehalten (**offen**).
- Nach dem Einschalten erscheint nach wenigen Sekunden die Startseite.
- **Netzspannung (230 V):** Wird das Panel an Netzspannung angeschlossen, darf die Installation nur eine Elektrofachperson mit Bewilligung nach **NIV** ausführen.

## Montage

- Unterputzmontage in einer Gerätedose. Welche Dose genau passt, ist hier noch offen (Herstellerangabe beachten).
- Vor Arbeiten an der Dose immer die Sicherung ausschalten.

## Besonderheiten

- Kein Relais. Die Taste **Schalter** auf der Startseite bedient deshalb einen Loxone-Baustein (Einstellungen › Orte & Infos › Schalter-Taste).
- Die Anschlüsse für RS485 und die Zusatz-Ein-/Ausgänge des Panels werden von dieser Firmware nicht verwendet. Bitte nichts daran anschliessen.
- Einen eingebauten Temperatur-/Feuchtesensor zeigt die Firmware nicht an.

## Updates

- **Firmware prüfen** (Einstellungen › System › Wartung & Update) findet neue Versionen für den Panlee automatisch, sobald ein neues Release erschienen ist.
- Alternativ spielst du eine Update-Datei von deinem Installateur über die Wartung im Browser ein ([allgemein.md](allgemein.md#8-wartung-im-browser-sicherung-wiederherstellung-firmware)). Das Panel nimmt nur Dateien an, die für den Panlee bestimmt sind.

## Bekannt

- Falls die Anzeige gelegentlich kurz flackert, bitte Version und Situation melden. Ab 0.9.10 sollte das nicht mehr auftreten, die Rückmeldung dazu steht noch aus.
