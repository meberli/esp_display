#pragma once
// User-visible texts outside panel_ui*.cpp (messages, status line, Loxone
// errors shown on tiles). One place to change wording or add a language.
// Serial log lines stay English and are not listed here.

namespace str {
// Boot / connection messages (ui_show_message title, body)
constexpr const char* kConnectingTitle = "Verbinden";
constexpr const char* kConnectingBody = "WLAN wird verbunden.";
constexpr const char* kLoadingTitle = "Laden";
constexpr const char* kLoadingBody = "Steuerungen werden vom Miniserver geladen (LoxAPP3.json).";
constexpr const char* kNotConfiguredTitle = "Nicht eingerichtet";
constexpr const char* kNotConfiguredBody =
    "WLAN- und Miniserver-Einstellungen fehlen.\n\n"
    "include/panel_config.example.h nach\n"
    "include/panel_config.h kopieren, den\n"
    "Miniserver eintragen, neu bauen und\n"
    "per USB flashen.\n\n"
    "Relais 1 bleibt aus, bis auf dem\n"
    "Startbildschirm unter Schalter\n"
    "Relais getippt wird.";
constexpr const char* kBadAuthBody = "LOXONE_AUTH muss \"basic\" oder \"token\" sein.";
constexpr const char* kOutOfMemoryTitle = "Zu wenig Speicher";
constexpr const char* kOutOfMemoryBody = "Kein Puffer fuer LoxAPP3.json verfuegbar.";
constexpr const char* kUnreachableTitle = "Miniserver nicht erreichbar";
constexpr const char* kLoadFailedTitle = "Loxone nicht lesbar";

// Status line
constexpr const char* kStatusConnecting = "WLAN wird verbunden";
constexpr const char* kStatusConnected = "WLAN verbunden";
constexpr const char* kStatusDisconnected = "WLAN getrennt";
constexpr const char* kStatusRelayOff = "Relais aus";
constexpr const char* kStatusLoxCached = "Loxone offline – gespeicherte Steuerung";
constexpr const char* kStatusLoxOffline = "Loxone nicht erreichbar – neuer Versuch läuft";
constexpr const char* kStatusLoxOnline = "Loxone verbunden";
constexpr const char* kLoxOfflineCmd = "Loxone offline";
constexpr const char* kWifiCancel = "Abbrechen";
constexpr const char* kWifiRetry = "Nochmals versuchen";
constexpr const char* kWifiOffline = "Ohne Netzwerk starten";
constexpr const char* kConnectCancel = "Abbrechen";
constexpr const char* kWifiRescan = "Neu suchen";
constexpr const char* kWifiScanCancelled = "Suche abgebrochen";
constexpr const char* kWifiFailedTitle = "WLAN nicht erreichbar";
constexpr const char* kStatusNoTouch = "Touch-Controller nicht gefunden";

// Loxone request errors (shown on a tile after a failed command)
constexpr const char* kLoxBadRequest = "Ungueltige Anfrage";
constexpr const char* kLoxWifiDown = "WLAN getrennt";
constexpr const char* kLoxLoginRejected = "Miniserver lehnt Login ab";
constexpr const char* kLoxEmpty = "Leere Antwort";
constexpr const char* kLoxIdRejected = "Steuerungs-ID abgelehnt";
constexpr const char* kLoxPathTooLong = "Pfad zu lang";
constexpr const char* kLoxCommandRejected = "Miniserver lehnt Befehl ab";
constexpr const char* kLoxStateNotJson = "Status-Antwort kein JSON";
constexpr const char* kLoxStateUnavailable = "Status nicht verfuegbar";
constexpr const char* kLoxStateNoValue = "Status ohne Wert";
constexpr const char* kLoxStateIdRejected = "Status-ID abgelehnt";
}  // namespace str
