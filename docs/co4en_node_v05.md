# CO4EN node side (Playground / CII), spec v0.5 notes for gwuno-fw

Status: implemented in `main/co4en.*` (pure, host-tested in `tools/test_co4en.cpp`) and
`main/espnow_play.cpp` (Playground). Wire format unchanged from v0.4: **no new frame type**.
Frames decode with `tools/co4en_ref.py` (`decode(f, keymap_tbl=...)`).

## Link loss
- Every ACK_REQ frame waits 20 ms, one app retry with a new seq. A frame without ACK-INFO
  after the retry is one *miss*; 3 consecutive misses (`kCo4enLinkMissMax`) = link lost
  (UI "Gateway verloren", DIAGCO4EN `state=lost|searching`).
- The 60 s STATUS heartbeat always carries ACK_REQ, so an idle node detects loss within ~3 min.
- Any ACK-INFO (any result) resets the miss counter.

## Probe / channel search
- Probe = normal encrypted unicast DATA (`STATUS` TLV 0x20, `ACK_REQ`) to the known gateway
  MAC, **new seq each probe**. Found = ACK-INFO for exactly that seq (MAC ACK alone is not enough).
- Order: last known channel (NVS `co4en/chan`), then 1, 6, 11, then 2..13 (no duplicates).
  One probe per channel, 50 ms wait (`kCo4enProbeWaitMs`).
- Not found: backoff 1, 5, 15, then 60 min (max), sweep restarts in the same order.
- CII (battery): sweeps only when VSTORE >= 15.5 V; the display simulates it
  (`CO4EN VSTORE <mV>`, default 16000).
- Found channel persisted in NVS only when it changed.
- Wi-Fi nodes (display, default): stay on the AP channel; the "sweep" is a single channel
  (same backoff). If ACK-INFO / PAIR_RESP reports another channel the log shows
  `gateway channel X != WLAN channel Y`.
- Unpaired nodes in CII mode: PAIR_REQ broadcast once per channel 1..13 (50 ms apart), then
  the normal 3 s / 30 s period. Fast phase restartable (button "Pairing 3 s neu", `CO4EN FAST`).

## CII mode on the display (test only)
- `CII-Modus (ohne WLAN)`, default off, not persisted, switches itself off after 30 min.
  On: `esp_wifi_disconnect()` + reconnects suppressed (credentials stay), ESP-NOW sets the
  channel with `esp_wifi_set_channel`. No whatwatt forward in this mode, manual values only.
  Off / reboot: normal reconnect.

## Off-channel while associated (IDF 6.1)
IDF 6.1 has `esp_now_switch_channel_tx()` (one frame on another channel, `wait_time_ms`)
and `esp_now_remain_on_channel()`. They are not described in the ESP-NOW guide, have no example,
and the guide still requires peers to be on the current channel. The frames go out as action frames
with no peer LMK encryption documented, and RX of the gateway's encrypted ACK-INFO during the
wait window is not specified. The display therefore does **not** use them for probes. `CO4EN OFFCHAN <ch>`
only checks that the call works and that Wi-Fi stays associated. See the D1 results in the commit message / report.

## KV-IDs (v0.5)
| key | id | type | OBIS |
|---|---|---|---|
| er.q1 | 30 | int64 varh | 5.8.0 (whatwatt import inductive) |
| er.q2 | 31 | int64 varh | 6.8.0 (export capacitive) |
| er.q3 | 32 | int64 varh | 7.8.0 (export inductive) |
| er.q4 | 33 | int64 varh | 8.8.0 (import capacitive) |
| w.vol | 40 | float64 m3 | - |
er.imp / er.exp (7/8) are only sent when the meter delivers the sums.
ACK result 1 (decode / unknown id): the node re-announces KEYMAP inline with the next frames.

## Gateway side needed
- Answer DATA+ACK_REQ from paired nodes with ACK-INFO on whatever channel it is on (peers stored
  with channel 0), including the current channel.
- ACK result 1 for unknown KV-IDs (node re-sends KEYMAP).
- Decode KV-IDs 30-33, 40.
- Accept seq jumps (+256 blocks after reboot) and probes with STATUS only.
