#pragma once
#ifdef ESP32
// ─────────────────────────────────────────────────────────────────────────────
// Seed (factory) Wi-Fi database entries embedded in firmware.
//
// Add your known reference points here.  They are imported into the
// on-device WifiDb on first run (if the DB file does not exist yet).
//
// Format for each entry:
//   { .bssid         = {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF},  // MAC bytes
//     .lat_i         = 559374000,   // 55.9374000 ° × 1e7
//     .lon_i         = 374218000,   // 37.4218000 ° × 1e7  (use negative for W)
//     .confidence    = 800,         // 0-1000  (800 = 80 %)
//     .sample_count  = 5,
//     .last_seen_unix= 0,           // 0 = unknown
//     .source        = (uint8_t)position::WifiDbSource::Manual,
//     ._pad          = 0 }
//
// Enable import: call WifiDb::importSeed() from setup code if !db.load().
// ─────────────────────────────────────────────────────────────────────────────

#include "PositionTypes.h"
#include <stdint.h>

namespace position {

struct SeedEntry {
    WifiDbRecord rec;
};

// Example seed entries.  Remove / replace with real data before deployment.
static const SeedEntry kSeedEntries[] = {
    // Example: office router at 55.9374°N, 37.4218°E
    {
        .rec = {
            .bssid          = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01},
            .lat_i          = 559374000,
            .lon_i          = 374218000,
            .confidence     = 800,
            .sample_count   = 5,
            .last_seen_unix = 0,
            .source         = (uint8_t)WifiDbSource::Manual,
            ._pad           = 0,
        }
    },
    // Add more entries here …
};

static constexpr uint8_t kSeedCount =
    (uint8_t)(sizeof(kSeedEntries) / sizeof(kSeedEntries[0]));

} // namespace position
#endif // ESP32
