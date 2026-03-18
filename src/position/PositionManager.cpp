#ifdef ESP32
#include "PositionManager.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <string.h>
#include <math.h>

#ifndef MESHTASTIC_EXCLUDE_GPS
#include "gps/GPS.h"
// gps is declared as std::unique_ptr<GPS> in GPS.h
#endif

namespace position {

// ─── construction ─────────────────────────────────────────────────────────────

PositionManager::PositionManager(WifiDb *db, WifiScanner *scanner)
    : OSThread("PositionManager", 0)
    , db_(db)
    , scanner_(scanner)
{
    setInterval(HYBRIDPOS_MIN_UPDATE_INTERVAL_MS);
    LOG_INFO("PositionManager: started (GNSS=%s LocalDB=%s RemoteAPI=%s)\n",
#ifndef MESHTASTIC_EXCLUDE_GPS
             "yes",
#else
             "no",
#endif
             db_ ? "yes" : "no",
             ENABLE_REMOTE_WIFI_GEO ? "yes" : "no");
}

// ─── OSThread ────────────────────────────────────────────────────────────────

int32_t PositionManager::runOnce()
{
    ++runCount_;
    pipelineErrors_ = 0;

    // ── Collect latest Wi-Fi scan results if ready ────────────────────────────
    if (scanner_ && scanner_->scanComplete()) {
        obsCount_ = scanner_->takeResults(obs_, WIFI_GEO_SCAN_TOP_N);
    }

    // ── Priority pipeline ─────────────────────────────────────────────────────
    PositionEstimate candidate;

    if (tryGnss(candidate)) {
        LOG_DEBUG("PositionManager: source=GNSS conf=%u\n", candidate.confidence);
#if ENABLE_WIFI_DB_LEARNING
        learnFromGnss(candidate);
#endif
    } else if (tryLocalDb(candidate)) {
        LOG_DEBUG("PositionManager: source=LocalWifiDb conf=%u\n", candidate.confidence);
    }
#if ENABLE_REMOTE_WIFI_GEO
    else if (tryRemoteApi(candidate)) {
        LOG_DEBUG("PositionManager: source=RemoteWifi conf=%u\n", candidate.confidence);
    }
#endif
    else if (tryLastKnown(candidate)) {
        LOG_DEBUG("PositionManager: source=LastKnown conf=%u age_s=%lu\n",
                  candidate.confidence,
                  (unsigned long)(getTime() - candidate.timestamp_sec));
    } else {
        LOG_INFO("PositionManager: no position available\n");
        return (int32_t)HYBRIDPOS_MIN_UPDATE_INTERVAL_MS;
    }

    best_ = candidate;

    // Persist last-known only if this is a "real" fix (not already last-known).
    if (candidate.source != PositionSource::LastKnown) {
        lastKnown_ = candidate;
    }

    commitToNodeDb(best_);
    return (int32_t)HYBRIDPOS_MIN_UPDATE_INTERVAL_MS;
}

// ─── providers ───────────────────────────────────────────────────────────────

bool PositionManager::tryGnss(PositionEstimate &out)
{
#ifdef MESHTASTIC_EXCLUDE_GPS
    return false;
#else
    if (!gps || !gps->isConnected() || !gps->hasLock()) return false;
    if (!gnssIsValid()) return false;

    out.lat_i         = gps->p.latitude_i;
    out.lon_i         = gps->p.longitude_i;
    out.altitude_m    = gps->p.altitude;
    out.accuracy_m    = gps->p.gps_accuracy > 0 ? gps->p.gps_accuracy / 1000 : 10;
    out.confidence    = 95;
    out.source        = PositionSource::GNSS;
    out.used_bssids   = 0;
    out.timestamp_sec = gps->p.timestamp ? gps->p.timestamp : (uint32_t)getTime();
    return true;
#endif
}

bool PositionManager::tryLocalDb(PositionEstimate &out)
{
    if (!db_ || obsCount_ == 0) return false;

    PositionEstimate est;
    if (!db_->lookup(obs_, obsCount_, est)) {
        LOG_DEBUG("PositionManager: no local DB matches\n");
        lastDbMatches_ = 0;
        return false;
    }
    lastDbMatches_ = est.used_bssids;
    out = est;
    return true;
}

bool PositionManager::tryRemoteApi(PositionEstimate &out)
{
#if !ENABLE_REMOTE_WIFI_GEO
    return false;
#else
    if (obsCount_ == 0) return false;
    PositionEstimate est;
    if (!remoteClient_.lookup(obs_, obsCount_, est)) {
        pipelineErrors_ |= 0x01;
        return false;
    }
    out = est;
    return true;
#endif
}

bool PositionManager::tryLastKnown(PositionEstimate &out)
{
    if (!lastKnown_.valid()) return false;
    uint32_t age = (uint32_t)(getTime() - lastKnown_.timestamp_sec);
    if (age > LAST_KNOWN_MAX_AGE_SEC) {
        LOG_DEBUG("PositionManager: last-known too old (%lu s)\n", (unsigned long)age);
        return false;
    }
    out             = lastKnown_;
    out.source      = PositionSource::LastKnown;
    // Confidence decays linearly with age.
    uint32_t maxAge = LAST_KNOWN_MAX_AGE_SEC;
    out.confidence  = (uint8_t)(lastKnown_.confidence * (maxAge - age) / maxAge);
    return true;
}

// ─── learning ────────────────────────────────────────────────────────────────

void PositionManager::learnFromGnss(const PositionEstimate &gnssPos)
{
#ifndef MESHTASTIC_EXCLUDE_GPS
    if (!db_ || obsCount_ == 0 || !gps) return;

    // Reject fixes that are too coarse or taken while moving.
    uint32_t hdop = gps->p.HDOP;
    if (hdop > GNSS_LEARNING_MAX_HDOP) {
        LOG_DEBUG("PositionManager: skip learning, HDOP=%lu > %u\n",
                  (unsigned long)hdop, GNSS_LEARNING_MAX_HDOP);
        return;
    }
    if (gps->p.ground_speed > GNSS_LEARNING_MAX_SPEED_MPS) {
        LOG_DEBUG("PositionManager: skip learning, speed=%lu m/s > %u\n",
                  (unsigned long)gps->p.ground_speed, GNSS_LEARNING_MAX_SPEED_MPS);
        return;
    }
    uint32_t fixAge = (uint32_t)(getTime() - gps->p.timestamp);
    if (gps->p.timestamp && fixAge > GNSS_LEARNING_MAX_AGE_SEC) {
        LOG_DEBUG("PositionManager: skip learning, fix age=%lu s > %u\n",
                  (unsigned long)fixAge, GNSS_LEARNING_MAX_AGE_SEC);
        return;
    }

    uint8_t updated = 0;
    for (uint8_t i = 0; i < obsCount_; ++i) {
        if (db_->update(obs_[i].bssid, gnssPos.lat_i, gnssPos.lon_i,
                        WifiDbSource::Local)) {
            ++updated;
        }
    }
    if (updated > 0) {
        LOG_INFO("PositionManager: learned %u BSSID(s) (DB total=%u)\n",
                 updated, db_->count());
    }
#endif
}

// ─── helpers ─────────────────────────────────────────────────────────────────

bool PositionManager::gnssIsValid() const
{
#ifdef MESHTASTIC_EXCLUDE_GPS
    return false;
#else
    if (!gps) return false;
    if (gps->p.HDOP > 0 && gps->p.HDOP > GNSS_REQUIRED_HDOP) {
        LOG_DEBUG("PositionManager: GNSS HDOP %lu > %u, not using\n",
                  (unsigned long)gps->p.HDOP, GNSS_REQUIRED_HDOP);
        return false;
    }
    if (gps->p.sats_in_view > 0 && gps->p.sats_in_view < GNSS_MIN_SATS) {
        LOG_DEBUG("PositionManager: GNSS sats %lu < %u, not using\n",
                  (unsigned long)gps->p.sats_in_view, GNSS_MIN_SATS);
        return false;
    }
    // Basic sanity: non-null coordinates.
    if (gps->p.latitude_i == 0 && gps->p.longitude_i == 0) return false;
    return true;
#endif
}

void PositionManager::commitToNodeDb(const PositionEstimate &est)
{
    meshtastic_Position pos = meshtastic_Position_init_default;
    pos.latitude_i   = est.lat_i;
    pos.longitude_i  = est.lon_i;
    pos.altitude     = est.altitude_m;
    pos.gps_accuracy = est.accuracy_m * 1000; // nodeDB stores in mm
    pos.time         = est.timestamp_sec;

    // Mark the location source so the mesh can see this is not a GPS fix
    // when the source is Wi-Fi-derived.
    switch (est.source) {
    case PositionSource::GNSS:
        pos.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;
        break;
    default:
        pos.location_source = meshtastic_Position_LocSource_LOC_EXTERNAL;
        break;
    }

    nodeDB->setLocalPosition(pos, /*timeOnly=*/false);
    LOG_DEBUG("PositionManager: committed to nodeDB (src=%u lat_i=%ld lon_i=%ld)\n",
              (unsigned)est.source, (long)est.lat_i, (long)est.lon_i);
}

} // namespace position
#endif // ESP32
