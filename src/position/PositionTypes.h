#pragma once
#include <stdint.h>

// ── Source enum ──────────────────────────────────────────────────────────────
enum class PositionSource : uint8_t {
    None        = 0,  ///< No position available
    GNSS        = 1,  ///< Hardware GNSS fix
    LocalWifiDb = 2,  ///< Match found in on-device Wi-Fi DB
    RemoteWifi  = 3,  ///< Result from external HTTP geo API
    LastKnown   = 4,  ///< Stale position retained as fallback
};

// ── Unified position estimate returned by any provider ───────────────────────
struct PositionEstimate {
    int32_t  lat_i          = 0;   ///< Latitude  × 1e7 (degrees)
    int32_t  lon_i          = 0;   ///< Longitude × 1e7 (degrees)
    int32_t  altitude_m     = 0;   ///< MSL altitude (metres); 0 if unknown
    uint32_t accuracy_m     = 9999;///< Horizontal accuracy estimate (metres)
    uint8_t  confidence     = 0;   ///< 0-100
    PositionSource source   = PositionSource::None;
    uint32_t timestamp_sec  = 0;   ///< Unix epoch seconds when the fix was obtained
    uint8_t  used_bssids    = 0;   ///< Number of BSSIDs used to compute position

    bool valid() const { return source != PositionSource::None && (lat_i != 0 || lon_i != 0); }
};

// ── Single observed Wi-Fi access point (result of one scan) ──────────────────
struct WifiObservation {
    uint8_t bssid[6]    = {};    ///< Raw MAC bytes
    int8_t  rssi        = -127;  ///< Received signal strength (dBm)
    uint8_t channel     = 0;
    char    ssid[33]    = {};    ///< Null-terminated; filled only for logging
};

// ── Record persisted in the on-device Wi-Fi DB ───────────────────────────────
// Fixed size = 24 bytes; do NOT change field layout between firmware versions
// without bumping WIFI_DB_VERSION.
enum class WifiDbSource : uint8_t {
    Unknown    = 0,
    Local      = 1,  ///< Learned automatically from GPS
    Manual     = 2,  ///< Inserted manually / seed database
    Api        = 3,  ///< Imported from external API result
    Wardriving = 4,
};

struct __attribute__((packed)) WifiDbRecord {
    uint8_t      bssid[6];         ///< MAC address
    int32_t      lat_i;            ///< degrees × 1e7
    int32_t      lon_i;            ///< degrees × 1e7
    uint16_t     confidence;       ///< 0-1000 (maps to 0.0-1.0 in 0.1 % steps)
    uint16_t     sample_count;     ///< capped at 65535
    uint32_t     last_seen_unix;   ///< epoch seconds
    uint8_t      source;           ///< WifiDbSource
    uint8_t      _pad;             ///< alignment padding
    // Total: 6+4+4+2+2+4+1+1 = 24 bytes
};
static_assert(sizeof(WifiDbRecord) == 24, "WifiDbRecord layout changed");

// ── File header for wifidb.bin ────────────────────────────────────────────────
static constexpr uint32_t WIFI_DB_MAGIC   = 0x57464442UL;  // "WFDB"
static constexpr uint8_t  WIFI_DB_VERSION = 1;

struct __attribute__((packed)) WifiDbFileHeader {
    uint32_t magic;    ///< must equal WIFI_DB_MAGIC
    uint8_t  version;  ///< must equal WIFI_DB_VERSION
    uint16_t count;    ///< number of records following the header
    uint8_t  _pad;
    // Total: 8 bytes
};
static_assert(sizeof(WifiDbFileHeader) == 8, "WifiDbFileHeader layout changed");

// ── Compact LoRa diagnostic packet (portnum PRIVATE_APP) ─────────────────────
// 16 bytes; sent on demand or periodically to convey positioning telemetry.
struct __attribute__((packed)) HybridPosDiagPacket {
    uint8_t  source;        ///< PositionSource cast to uint8_t
    uint8_t  confidence;    ///< 0-100
    int32_t  lat_i;         ///< degrees × 1e7
    int32_t  lon_i;         ///< degrees × 1e7
    uint16_t accuracy_m;    ///< capped at 65535
    uint8_t  visible_aps;   ///< count of APs from last scan
    uint8_t  db_matches;    ///< count of DB matches
    uint16_t bssid_hash;    ///< XOR fold of top-BSSID MAC bytes (fingerprint)
    uint8_t  error_code;    ///< last error (0 = none)
    uint8_t  _pad;          ///< alignment / reserved
    // Total: 18 bytes
};
static_assert(sizeof(HybridPosDiagPacket) == 18, "HybridPosDiagPacket layout changed");
