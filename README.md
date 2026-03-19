<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

## Differences from upstream (derunix fork)

This fork tracks [meshtastic/firmware](https://github.com/meshtastic/firmware) `develop` and adds the following changes:

### Heltec V4 — RF Front End
- **LNA enabled by default** (cherry-picked from weebl2000/heltec-v4.3-enable-lna-by-default): `LoRaFEMInterface` sets `PA_CTX LOW` at init so the KCT8103L LNA is active on startup. `NodeDB::installDefaultConfig` sets `FEM_LNA_Mode_ENABLED`. The `!= DISABLED` logic in `SX126xInterface` and `AdminModule` ensures the LNA stays on unless explicitly disabled.

### Battery monitoring (`BatteryTracker`)
New module `src/power/BatteryTracker` tracks battery consumption across charge/discharge cycles without a hardware current sensor:
- **Activity-based current estimation**: models CPU idle + LoRa TX bursts + GPS + WiFi contributions; constants configurable per variant (`BAT_BASE_CURRENT_MA`, `BAT_TX_CURRENT_MA`, `BAT_GPS_CURRENT_MA`, `BAT_WIFI_CURRENT_MA`)
- **Capacity learning**: EMA over completed discharge cycles (`soc_start` → `soc_end`); `learnedCapacityMah` converges after 3–5 cycles
- **Per-cycle log**: up to 5 cycles stored in LittleFS (`/prefs/battery.bin`) — duration, SoC start/end, TX/relay/RX packet counts, GPS active & fix seconds, capacity estimate
- **Cross-platform storage**: LittleFS (`FSCom`) instead of ESP32-only `Preferences` — works on NRF52, ESP32, STM32

### OLED battery screens (Heltec V4)
Four new frames registered in `Screen.cpp` when a battery is detected:
| Screen | Content |
|--------|---------|
| Battery 1/2 | Voltage, estimated current, 1/5/15-min averages |
| Battery 2/2 | Charged mAh, current mAh, used mAh, avg/day, estimated life + cycle count |
| Packet Stats | Live TX/relay/RX counters + last 4 historical cycles |
| GPS Stats | Live GPS state (fix/searching/disabled) + per-cycle active%, fix%, duration |

### Hybrid positioning subsystem (ESP32)

New pipeline in `src/position/` (guarded by `#ifdef ESP32`) tries four position sources in order:

| Priority | Source | Condition |
|----------|--------|-----------|
| 1 | GNSS | Valid fix (HDOP ≤ 2.0, ≥ 4 sats) |
| 2 | Local Wi-Fi DB | ≥ 1 known BSSID visible |
| 3 | Remote HTTP geo API | `ENABLE_REMOTE_WIFI_GEO=1` (off by default) |
| 4 | Last-known fallback | Age ≤ 1 h |

**Components:**
- **`WifiScanner`** — async `WiFi.scanNetworks()`, top-12 by RSSI, optional radio-off/restore
- **`WifiDb`** — LittleFS flat-file (`/prefs/wifidb.bin`), 24-byte records, EMA coordinate smoothing (α=25%), LRU eviction, up to 512 entries (~12 KB); seed BSSIDs importable via `WifiDbSeed.h`
- **`WifiGeoClient`** — `HTTPClient` lookup against Google Geolocation API format endpoint; disabled by default
- **`PositionManager`** — `OSThread` orchestrating the pipeline; auto-learns AP coordinates when GNSS fix is valid, stationary (≤ 3 m/s), and fresh (≤ 30 s)
- **`HybridPositionModule`** — `MeshModule` entry point registered in `Modules.cpp`; broadcasts an 18-byte `HybridPosDiagPacket` on `PRIVATE_APP` portnum

Weighted centroid: RSSI-based exponential weights (ref −40 dBm); confidence ≥ 0.7 when ≥ 3 DB matches. Scan interval 60 s, pipeline minimum interval 30 s. All thresholds overridable via `-D` flags in `platformio.ini`.

### M5Stack Cardputer-Adv (ESP32-S3FN8 + Cap LoRa868)

New board target `m5stack-cardputer-adv` for the M5Stack Cardputer fitted with the Cap LoRa868 module (SX1262 radio + ATGM336H-6N GPS):

**Hardware:**
- ESP32-S3FN8 @ 240 MHz, 8 MB flash, **no PSRAM** (`-DBOARD_HAS_PSRAM=0`, no `qio_opi`)
- TCA8418 7×8 key matrix (56-key QWERTY keyboard) on I2C with `KB_INT` interrupt pin
- SX1262 LoRa transceiver (Cap LoRa868), ATGM336H-6N GPS

**Stability fixes:**
- **TWDT crash fix**: removed `memory_type: qio_opi` from board JSON — the chip has no OPI PSRAM, causing NimBLE to stall during memory init and trip the 5 s watchdog at ~150 s uptime
- **WifiScanner BLE guard**: `WiFi.mode()` calls are now skipped while BLE controller is initialising (`esp_bt_controller_get_status() != IDLE`); deferred `WiFi.mode(OFF)` via `pendingModeOff_` flag when BLE is busy post-scan; `esp_task_wdt_reset()` wraps every blocking `WiFi.mode()` call

**Keyboard responsiveness:**
- `KB_INT` FALLING interrupt attached after `cardKbI2cImpl->init()` — key presses wake `runOnce()` immediately instead of waiting for the poll timer
- `KbI2cBase::runOnce()` fallback poll interval reduced 300 ms → 30 ms
- After `clearInt()`, FIFO is re-checked and `runOnce()` returns 0 if new events arrived during dispatch — prevents missed keys when INT stays low

**Text input:**
- Welcome banner "OK" / Enter fixed: `ui->getUiState()->lastUpdate = 0` forces immediate frame draw so the overlay callback processes the SELECT event in the same tick
- **EN/RU keyboard layout** (`Opt+Alt` to toggle):
  - Russian QWERTY table (a–z / A–Z → UTF-8 Cyrillic) inserted directly into `freetext` preserving 2-byte sequences
  - Backspace is UTF-8-aware: walks back over continuation bytes to delete the whole Cyrillic glyph
  - Layout badge (**EN** / **RU**) + `Opt+Alt` hint shown in the bottom bar of the text-input screen
  - `OLED_RU` font enabled so Cyrillic renders correctly on the OLED display

### Status bar improvements
- **Home screen**: voltage (`3.82V`) shown in the title area instead of a second `%` value
- **All screens**: charging indicator — `83%+` when USB/charger connected
- Battery icon (`icon_battery`) added to `images.h`

### Power menu additions
- **Reset Battery History** — wipes `/prefs/battery.bin` and resets the in-RAM state; confirmation prompt
- **Low V Protect** — configurable low-voltage shutdown timeout: Off / 5 min / 10 min (default) / 15 min / 30 min; saved in LittleFS
- **Battery screen context menu** — holding the select button on any of the 4 battery/stats frames opens the Power Menu (reboot, shutdown, reset battery history, low-V protect). Fixed missing `battery` position in `FramePositions` and missing input handler branch in `handleInputEvent()`.

### Low-voltage shutdown fixes
- **OCV array expanded to 17 points** (`power.h`): range 4250–2700 mV (was 11 points, min 3420 mV). `OCV[NUM_OCV_POINTS]` array size uses the macro instead of hardcoded `11`. Low-battery threshold now correctly triggers at 2700 mV.
- **Voltage-trend USB detection** (`Power.cpp`): boards without `EXT_PWR_DETECT` (e.g. Heltec V4) detect charging via rising voltage (+5 mV/sample). Counter resets if voltage is rising, preventing false shutdown while charging from a low state.

### InkHUD Battery Applet (Heltec Mesh Pocket)
`src/graphics/niche/InkHUD/Applets/User/Battery/` — new applet showing voltage, current estimate, charged/remaining mAh, and estimated life.

### Display Text

On-device displays for some builds transliterate non-ASCII text to ASCII using an ICAO-style Cyrillic mapping plus basic
Greek. This only affects UI rendering; radio/app payloads remain UTF-8. Limitations: Yo (U+0401/U+0451) maps to E/e, short
I (U+0419/U+0439) maps to I/i, hard/soft signs (U+042A/U+044A, U+042C/U+044C) are omitted, extra Cyrillic letters map to
simple digraphs (e.g., Ye, Yi, Lj), Greek tonos/diaeresis are ignored, and other non-ASCII becomes '?'.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")
