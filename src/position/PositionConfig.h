#pragma once
// Compile-time configuration for the hybrid positioning subsystem.
// All values can be overridden via -D flags in platformio.ini.

// ── Wi-Fi scan ───────────────────────────────────────────────────────────────
// Interval between background Wi-Fi scans (ms).
#ifndef WIFI_GEO_SCAN_INTERVAL_MS
#define WIFI_GEO_SCAN_INTERVAL_MS       60000U
#endif

// Maximum number of APs to use after sorting by RSSI (top-N strongest).
#ifndef WIFI_GEO_SCAN_TOP_N
#define WIFI_GEO_SCAN_TOP_N             12U
#endif

// Attempt a scan even if Meshtastic has not initialised WiFi yet.
// Set to 0 to only scan when WiFi is already active (STA/AP/AP_STA).
#ifndef WIFI_GEO_SCAN_WHEN_RADIO_OFF
#define WIFI_GEO_SCAN_WHEN_RADIO_OFF    1
#endif

// Maximum milliseconds to wait for an async scan to complete.
#ifndef WIFI_GEO_SCAN_TIMEOUT_MS
#define WIFI_GEO_SCAN_TIMEOUT_MS        10000U
#endif

// ── Position pipeline ────────────────────────────────────────────────────────
// Minimum interval between position-pipeline runs (ms).
#ifndef HYBRIDPOS_MIN_UPDATE_INTERVAL_MS
#define HYBRIDPOS_MIN_UPDATE_INTERVAL_MS  30000U
#endif

// ── Remote geo API ───────────────────────────────────────────────────────────
// Enable/disable the remote HTTP lookup at compile time.
#ifndef ENABLE_REMOTE_WIFI_GEO
#define ENABLE_REMOTE_WIFI_GEO          0
#endif

// API endpoint.  Google Geolocation API format is assumed.
#ifndef REMOTE_GEO_URL
#define REMOTE_GEO_URL                  "https://www.googleapis.com/geolocation/v1/geolocate"
#endif

// API key appended as ?key=… query parameter (empty → no key appended).
#ifndef REMOTE_GEO_API_KEY
#define REMOTE_GEO_API_KEY              ""
#endif

// HTTP connect + read timeout (ms).
#ifndef REMOTE_GEO_TIMEOUT_MS
#define REMOTE_GEO_TIMEOUT_MS           6000U
#endif

// ── Local Wi-Fi DB ───────────────────────────────────────────────────────────
// Enable auto-learning: update DB while GPS fix is valid.
#ifndef ENABLE_WIFI_DB_LEARNING
#define ENABLE_WIFI_DB_LEARNING         1
#endif

// Maximum number of records stored on flash.  Each record is 24 bytes.
// 512 × 24 = 12 288 bytes ≈ 12 KB on LittleFS.
#ifndef MAX_WIFI_DB_RECORDS
#define MAX_WIFI_DB_RECORDS             512U
#endif

// LittleFS path for the database file.
#ifndef WIFI_DB_PATH
#define WIFI_DB_PATH                    "/prefs/wifidb.bin"
#endif

// How often (in pipeline runs) the in-memory DB is flushed to flash.
#ifndef WIFI_DB_FLUSH_INTERVAL
#define WIFI_DB_FLUSH_INTERVAL          10U
#endif

// ── GNSS quality thresholds ──────────────────────────────────────────────────
// Maximum HDOP (×100) accepted as a valid fix.  200 = HDOP 2.0.
#ifndef GNSS_REQUIRED_HDOP
#define GNSS_REQUIRED_HDOP              200U
#endif

// Minimum number of satellites for a valid fix.
#ifndef GNSS_MIN_SATS
#define GNSS_MIN_SATS                   4U
#endif

// Stricter HDOP for DB learning (avoids poisoning the DB with coarse fixes).
#ifndef GNSS_LEARNING_MAX_HDOP
#define GNSS_LEARNING_MAX_HDOP          150U
#endif

// Maximum ground speed (m/s) allowed during DB learning.
// Prevents learning AP positions while device is mobile.
#ifndef GNSS_LEARNING_MAX_SPEED_MPS
#define GNSS_LEARNING_MAX_SPEED_MPS     3U
#endif

// Maximum age of a GNSS fix accepted for DB learning (seconds).
#ifndef GNSS_LEARNING_MAX_AGE_SEC
#define GNSS_LEARNING_MAX_AGE_SEC       30U
#endif

// ── Last-known fallback ───────────────────────────────────────────────────────
// Maximum age of the last-known position before it is discarded (seconds).
#ifndef LAST_KNOWN_MAX_AGE_SEC
#define LAST_KNOWN_MAX_AGE_SEC          3600U
#endif

// ── Centroid weighting ────────────────────────────────────────────────────────
// RSSI reference level (dBm) treated as "100% signal weight".
// Weight degrades exponentially below this value.
#ifndef WIFI_RSSI_REF_DBM
#define WIFI_RSSI_REF_DBM               (-40)
#endif

// Minimum number of DB matches required to report high confidence (≥0.7).
#ifndef WIFI_MIN_MATCHES_HIGH_CONF
#define WIFI_MIN_MATCHES_HIGH_CONF      3U
#endif

// Exponential Moving Average alpha for coordinate smoothing in the DB.
// New value weight = WIFI_DB_EMA_ALPHA / 256.  128 = 50 %, 64 = 25 %.
#ifndef WIFI_DB_EMA_ALPHA
#define WIFI_DB_EMA_ALPHA               64U
#endif
