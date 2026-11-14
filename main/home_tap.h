#pragma once
// Pure helpers for home / category taps: if a feature is not ready, the tap
// opens its setup screen; otherwise it opens the normal view. Host-tested.

// True when the tile should open the setup screen rather than the view.
inline bool home_opens_setup(bool configured) { return !configured; }

// Energie / PV home tile. The tile shows "einrichten" when whatwatt is not
// ready (on && host). Co4 can still serve the energy view without a local
// whatwatt. PV being on does not count as whatwatt ready.
inline bool energy_home_opens_setup(bool whatwatt_ready, bool co4_ready) {
  return !whatwatt_ready && !co4_ready;
}

inline bool loxone_home_opens_setup(bool miniserver_configured) { return !miniserver_configured; }

// Cameras need at least one camera and a Windy key (firmware default or own).
inline bool cameras_home_opens_setup(bool have_camera, bool have_key) { return !have_camera || !have_key; }
