#pragma once
#ifdef ESP32

#include "PositionConfig.h"
#include "PositionTypes.h"
#include <stdint.h>

namespace position {

// Manages a persistent on-device database of BSSID → location mappings.
//
// Storage: LittleFS binary file (WIFI_DB_PATH).
// All records are loaded into a heap-allocated array at startup.
// Lookups are linear (O(n)) which is fast enough for ≤512 records.
// Dirty records are flushed to flash every WIFI_DB_FLUSH_INTERVAL calls to
// update() or whenever flush() is called explicitly.
class WifiDb {
  public:
    WifiDb();
    ~WifiDb();

    // Load records from flash.  Call once after FS is mounted.
    bool load();

    // Flush dirty records to flash.  Returns false on write error.
    bool flush();

    // Look up the set of visible APs against the database.
    // Computes a weighted centroid from all matching records.
    // Returns true and fills `out` if at least one match is found.
    bool lookup(const WifiObservation *obs, uint8_t obsCount,
                PositionEstimate &out) const;

    // Import a static array of seed records (only inserts, does not overwrite
    // existing records).  Call after load() if the DB was empty.
    void importSeed(const WifiDbRecord *seed, uint8_t count);

    // Update (or insert) a record for `bssid` using the provided coordinates.
    // Coordinates are smoothed via EMA; confidence and sample_count are
    // incremented.  Returns false if the DB is full and eviction failed.
    bool update(const uint8_t bssid[6], int32_t lat_i, int32_t lon_i,
                WifiDbSource src = WifiDbSource::Local);

    uint16_t count()    const { return count_; }
    uint16_t capacity() const { return capacity_; }

  private:
    // Find index of record matching bssid, or -1 if not found.
    int find(const uint8_t bssid[6]) const;

    // Evict the oldest record to make room.  Returns evicted index.
    int evict();

    WifiDbRecord *records_     = nullptr;
    uint16_t      count_       = 0;
    uint16_t      capacity_    = 0;      ///< allocated slots (== MAX_WIFI_DB_RECORDS)
    bool          dirty_       = false;
    uint8_t       flushTick_   = 0;
};

} // namespace position
#endif // ESP32
