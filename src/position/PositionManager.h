#pragma once
#ifdef ESP32

#include "PositionConfig.h"
#include "PositionTypes.h"
#include "WifiDb.h"
#include "WifiScanner.h"
#include "concurrency/OSThread.h"
#if ENABLE_REMOTE_WIFI_GEO
#include "WifiGeoClient.h"
#endif
#include <stdint.h>

namespace position {

// Orchestrates the four-level position pipeline:
//
//   1. GNSS             – if fix valid and fresh
//   2. Local Wi-Fi DB   – weighted centroid from visible BSSIDs
//   3. Remote Wi-Fi API – HTTP lookup (only if ENABLE_REMOTE_WIFI_GEO=1)
//   4. Last-known       – stale position retained if within max age
//
// On each run, the best available estimate is stored in bestEstimate_ and
// pushed to nodeDB->setLocalPosition() so the existing PositionModule can
// broadcast it on the mesh.
//
// Auto-learning: when a valid GNSS fix is available *and* a Wi-Fi scan has
// results, the visible BSSIDs are used to update the on-device WifiDb.
class PositionManager : private concurrency::OSThread {
  public:
    explicit PositionManager(WifiDb *db, WifiScanner *scanner);
    ~PositionManager() = default;

    // Most recent position estimate (may be invalid if no source succeeded yet).
    const PositionEstimate &best() const { return best_; }

    // Last raw Wi-Fi scan observations.
    const WifiObservation *lastObs()   const { return obs_; }
    uint8_t               lastObsCount() const { return obsCount_; }

    // Number of DB matches from the last pipeline run.
    uint8_t               lastDbMatches() const { return lastDbMatches_; }

  protected:
    int32_t runOnce() override;

  private:
    // ── Position provider methods (return true and fill `out` on success) ──
    bool tryGnss(PositionEstimate &out);
    bool tryLocalDb(PositionEstimate &out);
    bool tryRemoteApi(PositionEstimate &out);
    bool tryLastKnown(PositionEstimate &out);

    // ── Learning ──────────────────────────────────────────────────────────────
    void learnFromGnss(const PositionEstimate &gnssPos);

    // ── Helpers ───────────────────────────────────────────────────────────────
    // Validate a GNSS fix against quality thresholds.
    bool gnssIsValid() const;

    // Push estimate into nodeDB as local position.
    void commitToNodeDb(const PositionEstimate &est);

    WifiDb      *db_      = nullptr;
    WifiScanner *scanner_ = nullptr;
#if ENABLE_REMOTE_WIFI_GEO
    WifiGeoClient remoteClient_;
#endif

    PositionEstimate best_;
    PositionEstimate lastKnown_;

    WifiObservation  obs_[WIFI_GEO_SCAN_TOP_N];
    uint8_t          obsCount_       = 0;
    uint8_t          lastDbMatches_  = 0;
    uint8_t          pipelineErrors_ = 0;   ///< last error code for diagnostics
    uint32_t         runCount_       = 0;
};

} // namespace position
#endif // ESP32
