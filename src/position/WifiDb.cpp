#ifdef ESP32
#include "WifiDb.h"
#include "FSCommon.h"
#include "RTC.h"         // getTime()
#include "configuration.h"
#include <math.h>
#include <string.h>

namespace position {

// ─── construction ─────────────────────────────────────────────────────────────

WifiDb::WifiDb()
{
    capacity_ = (uint16_t)MAX_WIFI_DB_RECORDS;
    records_  = new WifiDbRecord[capacity_];
    if (!records_) {
        LOG_ERROR("WifiDb: OOM allocating record array (%u records)\n", capacity_);
        capacity_ = 0;
    }
}

WifiDb::~WifiDb()
{
    if (dirty_) flush();
    delete[] records_;
}

// ─── persistence ──────────────────────────────────────────────────────────────

bool WifiDb::load()
{
    if (!records_) return false;

    File f = FSCom.open(WIFI_DB_PATH, FILE_O_READ);
    if (!f) {
        LOG_INFO("WifiDb: %s not found, starting empty\n", WIFI_DB_PATH);
        return true; // not an error – first run
    }

    WifiDbFileHeader hdr;
    if (f.read((uint8_t *)&hdr, sizeof(hdr)) != sizeof(hdr)) {
        LOG_WARN("WifiDb: truncated header, ignoring file\n");
        f.close();
        return false;
    }
    if (hdr.magic != WIFI_DB_MAGIC || hdr.version != WIFI_DB_VERSION) {
        LOG_WARN("WifiDb: incompatible header (magic=%08lx ver=%u), starting empty\n",
                 (unsigned long)hdr.magic, hdr.version);
        f.close();
        return false;
    }

    uint16_t toRead = hdr.count < capacity_ ? hdr.count : capacity_;
    size_t   bytes  = f.read((uint8_t *)records_, toRead * sizeof(WifiDbRecord));
    f.close();

    count_ = (uint16_t)(bytes / sizeof(WifiDbRecord));
    LOG_INFO("WifiDb: loaded %u/%u records from %s\n", count_, hdr.count, WIFI_DB_PATH);
    return true;
}

bool WifiDb::flush()
{
    if (!records_ || !dirty_) return true;

    File f = FSCom.open(WIFI_DB_PATH, FILE_O_WRITE);
    if (!f) {
        LOG_ERROR("WifiDb: cannot open %s for writing\n", WIFI_DB_PATH);
        return false;
    }

    WifiDbFileHeader hdr;
    hdr.magic   = WIFI_DB_MAGIC;
    hdr.version = WIFI_DB_VERSION;
    hdr.count   = count_;
    hdr._pad    = 0;

    f.write((const uint8_t *)&hdr, sizeof(hdr));
    f.write((const uint8_t *)records_, count_ * sizeof(WifiDbRecord));
    f.close();

    dirty_ = false;
    LOG_DEBUG("WifiDb: flushed %u records to %s\n", count_, WIFI_DB_PATH);
    return true;
}

// ─── seed import ──────────────────────────────────────────────────────────────

void WifiDb::importSeed(const WifiDbRecord *seed, uint8_t count)
{
    uint8_t added = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (find(seed[i].bssid) >= 0) continue; // already known
        if (update(seed[i].bssid, seed[i].lat_i, seed[i].lon_i,
                   (WifiDbSource)seed[i].source)) {
            // Patch the freshly-inserted record with seed metadata.
            int idx = find(seed[i].bssid);
            if (idx >= 0) {
                records_[idx].confidence   = seed[i].confidence;
                records_[idx].sample_count = seed[i].sample_count;
                if (seed[i].last_seen_unix)
                    records_[idx].last_seen_unix = seed[i].last_seen_unix;
            }
            ++added;
        }
    }
    if (added > 0) {
        dirty_ = true;
        LOG_INFO("WifiDb: imported %u seed record(s)\n", added);
    }
}

// ─── lookup ───────────────────────────────────────────────────────────────────

bool WifiDb::lookup(const WifiObservation *obs, uint8_t obsCount,
                    PositionEstimate &out) const
{
    if (!records_ || obsCount == 0) return false;

    // Accumulate a weighted centroid.
    double wLatSum  = 0.0;
    double wLonSum  = 0.0;
    double wSum     = 0.0;
    uint8_t matches = 0;

    for (uint8_t i = 0; i < obsCount; ++i) {
        int idx = find(obs[i].bssid);
        if (idx < 0) continue;

        const WifiDbRecord &r = records_[idx];

        // Weight = rssi_factor × confidence_factor × sample_factor
        // rssi_factor: 2^((rssi - ref) / 10)  →  exponential path-loss model
        double rssiOffset   = (double)(obs[i].rssi - WIFI_RSSI_REF_DBM) / 10.0;
        double rssiWeight   = pow(2.0, rssiOffset < 0.0 ? rssiOffset : 0.0);
        double confWeight   = (double)r.confidence / 1000.0;
        double sampleWeight = r.sample_count < 10 ? r.sample_count / 10.0 : 1.0;
        double w            = rssiWeight * confWeight * sampleWeight;

        if (w <= 0.0) continue;

        wLatSum += (double)r.lat_i * w;
        wLonSum += (double)r.lon_i * w;
        wSum    += w;
        ++matches;
    }

    if (matches == 0 || wSum == 0.0) return false;

    out.lat_i      = (int32_t)(wLatSum / wSum);
    out.lon_i      = (int32_t)(wLonSum / wSum);
    out.altitude_m = 0; // DB does not store altitude
    out.source     = PositionSource::LocalWifiDb;
    out.used_bssids = matches;
    out.timestamp_sec = (uint32_t)getTime();

    // Confidence and accuracy estimate:
    //   Many close-together matches → high confidence, tight accuracy estimate.
    //   Few matches → low confidence, coarse accuracy.
    if (matches >= WIFI_MIN_MATCHES_HIGH_CONF) {
        out.confidence = 70;
        out.accuracy_m = 80;
    } else if (matches == 2) {
        out.confidence = 50;
        out.accuracy_m = 200;
    } else {
        // Single match – coarse position
        out.confidence = 30;
        out.accuracy_m = 500;
    }

    LOG_INFO("WifiDb: %u match(es), conf=%u%%, acc≈%um, lat_i=%ld lon_i=%ld\n",
             matches, out.confidence, out.accuracy_m,
             (long)out.lat_i, (long)out.lon_i);
    return true;
}

// ─── update / learn ───────────────────────────────────────────────────────────

bool WifiDb::update(const uint8_t bssid[6], int32_t lat_i, int32_t lon_i,
                    WifiDbSource src)
{
    if (!records_) return false;

    uint32_t now = (uint32_t)getTime();
    int idx      = find(bssid);

    if (idx >= 0) {
        // Update existing record with EMA smoothing.
        WifiDbRecord &r = records_[idx];
        const uint8_t alpha = WIFI_DB_EMA_ALPHA; // 0-255; higher = more weight on new data
        r.lat_i = (int32_t)(((uint32_t)alpha * (uint32_t)lat_i +
                              (uint32_t)(256 - alpha) * (uint32_t)r.lat_i) >> 8);
        r.lon_i = (int32_t)(((uint32_t)alpha * (uint32_t)lon_i +
                              (uint32_t)(256 - alpha) * (uint32_t)r.lon_i) >> 8);
        r.last_seen_unix = now;
        if (r.sample_count < 65535) ++r.sample_count;
        // Confidence grows slowly up to 1000 (never exceeds it).
        if (r.confidence < 1000 - 10) r.confidence += 10;
        else r.confidence = 1000;
        dirty_ = true;
        LOG_DEBUG("WifiDb: updated BSSID %02x:%02x:%02x:… count=%u\n",
                  bssid[0], bssid[1], bssid[2], r.sample_count);
    } else {
        // Insert new record.
        if (count_ >= capacity_) {
            int ev = evict();
            if (ev < 0) {
                LOG_WARN("WifiDb: full and eviction failed\n");
                return false;
            }
            idx = ev;
        } else {
            idx = (int)count_++;
        }
        WifiDbRecord &r = records_[idx];
        memcpy(r.bssid, bssid, 6);
        r.lat_i          = lat_i;
        r.lon_i          = lon_i;
        r.confidence     = 100;   // start at 10 %
        r.sample_count   = 1;
        r.last_seen_unix = now;
        r.source         = (uint8_t)src;
        r._pad           = 0;
        dirty_           = true;
        LOG_DEBUG("WifiDb: inserted BSSID %02x:%02x:%02x:… (total=%u)\n",
                  bssid[0], bssid[1], bssid[2], count_);
    }

    // Periodic flush
    if (++flushTick_ >= WIFI_DB_FLUSH_INTERVAL) {
        flushTick_ = 0;
        flush();
    }
    return true;
}

// ─── private helpers ──────────────────────────────────────────────────────────

int WifiDb::find(const uint8_t bssid[6]) const
{
    for (uint16_t i = 0; i < count_; ++i) {
        if (memcmp(records_[i].bssid, bssid, 6) == 0) return (int)i;
    }
    return -1;
}

int WifiDb::evict()
{
    if (count_ == 0) return -1;
    // Evict the record with the oldest last_seen_unix and lowest sample_count.
    uint16_t best = 0;
    uint32_t bestScore = records_[0].last_seen_unix + records_[0].sample_count;
    for (uint16_t i = 1; i < count_; ++i) {
        uint32_t s = records_[i].last_seen_unix + records_[i].sample_count;
        if (s < bestScore) { bestScore = s; best = i; }
    }
    // Overwrite evicted slot with the last record to keep the array packed.
    if (best != count_ - 1) {
        records_[best] = records_[count_ - 1];
    }
    --count_;
    LOG_DEBUG("WifiDb: evicted slot %u (now %u records)\n", best, count_);
    return (int)best; // caller uses this slot for the new record
}

} // namespace position
#endif // ESP32
