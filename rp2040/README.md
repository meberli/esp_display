# SenseCAP Indicator RP2040 firmware (Playground scope)

`indicator_rp2040_scope/` is Seeed's factory RP2040 sketch
([SenseCAP_Indicator_RP2040](https://github.com/Seeed-Solution/SenseCAP_Indicator_RP2040),
`examples/indicator_rp2040` v1.0.1, **GPL-3.0**, so this sketch is GPL-3.0-only as well,
see `indicator_rp2040_scope/LICENSE` and `NOTICE`)
plus the Playground oscilloscope / logic analyzer (`scope_rp2040.*`).
Sensors (SCD41, SGP40, AHT20), SD logging, buzzer and the sensor packets
0xB2..0xB5 are unchanged; the 5 s sensor round only runs while no capture is active.

`libraries/RpScope/` (protocol, COBS, measurements, decimation; own code, **MIT**,
`libraries/RpScope/LICENSE`) is shared with
the ESP32 firmware (`main/rp_link.cpp`, `main/scope_view.cpp`) and the host
test `tools/test_scope.cpp`.

## Inputs — max 3.3 V

| Function | Grove port | RP2040 pin |
|---|---|---|
| Oszi A0 | Grove ADC | GPIO26 (ADC0) |
| Oszi A1 | Grove ADC | GPIO27 (ADC1) |
| Logik D0 | Grove I2C SDA | GPIO20 |
| Logik D1 | Grove I2C SCL | GPIO21 |
| Logik D2 | Grove ADC | GPIO26 |
| Logik D3 | Grove ADC | GPIO27 |

**Never more than 3.3 V or less than 0 V on any pin** (this destroys the
RP2040). Higher voltages only through a voltage divider (e.g. 10 k / 3.3 k for
0–12 V); never mains. Connect the ground of the circuit to Grove GND.
GPIO20/21 carry the internal I2C bus (sensors, pull-ups): the logic analyzer
only reads them (sniffing works), but a signal driven onto them disturbs the sensors.

## Specs

* Analog: RP2040 ADC, 12 bit, 0–3.3 V, 500 kS/s (48 MHz / 96); A0+A1 round robin
  250 kS/s each. Up to 61'440 samples per capture (30'720 per channel in dual mode).
  Clean display up to ~50 kHz (10 samples per period), Nyquist 250 kHz.
  Software edge trigger with ~20 mV hysteresis, pre-trigger 10/50/90 %, auto / normal.
* Logic: PIO samples GPIO20..27 at clk_sys / div, max 100 MS/s (clk_sys 200 MHz,
  div 2), 4 channels, up to 126'976 samples (1.27 ms at 100 MS/s). Hardware edge
  trigger (second state machine at 200 MHz, exact sample found in the data).
* Link: UART 115200 baud to the ESP32-S3 (unchanged from Seeed). The RP2040 sends
  450 decimated columns (min/max per column, ~1.9 KB for two analog channels,
  ~0.6 KB for logic), so a frame takes ~170 ms / ~50 ms on the wire; measurements
  (Vmin/Vmax/Vmean/f, logic f/duty/edges) are computed on the full capture.

## Build

```sh
arduino-cli core install rp2040:rp2040          # earlephilhower arduino-pico (tested 6.3.2)
arduino-cli lib install PacketSerial "Sensirion I2C SGP40" "Sensirion I2C SCD4x" \
    "Sensirion Gas Index Algorithm" "Sensirion Core" Seeed_Arduino_AHT20
arduino-cli compile -b rp2040:rp2040:seeed_indicator_rp2040:freq=200 \
    --libraries libraries --output-dir out indicator_rp2040_scope
```

Output: `out/indicator_rp2040_scope.ino.uf2`.

## Flashing (manual, needs physical access)

The ESP32 firmware works without this: the scope page then says the RP2040 does
not answer. To install, put the RP2040 into its USB boot loader (BOOTSEL, see
Seeed's wiki for the SenseCAP Indicator RP2040) so it shows up as the `RPI-RP2`
drive, and copy the `.uf2` onto it. Going back: copy Seeed's factory UF2 the same way.
