#include "BatteryTracker.h"
#include "configuration.h"
#include "RTC.h"
#include "mesh/RadioLibInterface.h"

#include <algorithm>
#include <cstring>

#include "FSCommon.h"
#include "SPILock.h"

#ifdef HAS_GPS
#include "GPSStatus.h"
extern meshtastic::GPSStatus *gpsStatus;
#endif

#if defined(HAS_WIFI) && defined(ARCH_ESP32)
#include <WiFi.h>
#endif

// ─── Persistent storage (LittleFS via FSCom — works on ESP32, NRF52, STM32) ──
static constexpr char BAT_FILENAME[] = "/prefs/battery.bin";
// Magic encodes the struct layout version. Bump when BatteryCycleNvs changes.
static constexpr uint32_t BAT_FILE_MAGIC = 0x42415402; // 'BAT' v2

struct BatteryFileV2 {
    uint32_t        magic;
    uint32_t        learnedCapacityMah;
    uint32_t        chargedMah;
    uint8_t         cycleCount;
    uint8_t         cyclesFilled;
    uint8_t         lvProtectMins;
    uint8_t         _pad;
    float           avgDailyMah;
    BatteryCycleNvs cycles[BAT_MAX_CYCLES];
};

// ─── Singleton ────────────────────────────────────────────────────────────────
BatteryTracker BatteryTracker::inst_;

BatteryTracker *BatteryTracker::instance()
{
    return &inst_;
}

// ─── setup ───────────────────────────────────────────────────────────────────
void BatteryTracker::setup()
{
    if (setupDone_)
        return;
    setupDone_ = true;
    loadFromNvs();
}

// ─── update (called every ~30s from Power::readPowerStatus) ──────────────────
void BatteryTracker::update(float voltV, uint8_t socPct, bool isCharging,
                            uint32_t txTotal, uint32_t relayTotal, uint32_t rxTotal)
{
    if (!setupDone_)
        return;

    uint32_t nowSec = getTime();
    if (nowSec == 0) {
        // RTC not ready yet — use millis-based fallback seconds
        nowSec = millis() / 1000;
    }

    // Throttle: skip if called too soon
    if (lastUpdateSec_ > 0 && (nowSec - lastUpdateSec_) < BAT_UPDATE_INTERVAL_S / 2)
        return;

    const uint32_t intervalSec = (lastUpdateSec_ > 0)
        ? std::min(nowSec - lastUpdateSec_, (uint32_t)120)
        : BAT_UPDATE_INTERVAL_S;

    // ── Detect charge transitions ───────────────────────────────────────────
    bool wasCharging = lastCharging_;
    if (wasCharging && !isCharging && lastSocPct_ > 50) {
        // Charger just disconnected at reasonable SoC → start discharge cycle
        if (learnedCapacityMah_ > 0)
            chargedMah_ = getCurrentMah();
        startCycle(socPct, txTotal, relayTotal, rxTotal);
    }
    if (!wasCharging && isCharging && inDischarge_) {
        // Plugged in → end discharge cycle
        onCycleEnd(socPct);
    }

    // ── Low battery shutdown detected ──────────────────────────────────────
    // If SoC drops below 5% while in discharge, close the cycle
    if (inDischarge_ && socPct < 5 && !isCharging) {
        onCycleEnd(socPct);
    }

    lastCharging_   = isCharging;
    lastVoltV_      = voltV;
    lastSocPct_     = socPct;

    if (isCharging) {
        // Not tracking consumption while charging
        lastTxTotal_    = txTotal;
        lastRelayTotal_ = relayTotal;
        lastRxTotal_    = rxTotal;
        lastUpdateSec_  = nowSec;
        return;
    }

    // ── Estimate current consumption ───────────────────────────────────────
    uint32_t deltaTx    = (txTotal    >= lastTxTotal_)    ? txTotal    - lastTxTotal_    : 0;
    uint32_t deltaRelay = (relayTotal >= lastRelayTotal_) ? relayTotal - lastRelayTotal_ : 0;
    uint32_t deltaRx    = (rxTotal    >= lastRxTotal_)    ? rxTotal    - lastRxTotal_    : 0;

    float estimatedMa = estimateCurrentMa(deltaTx, deltaRx);
    pushCurrent(estimatedMa);

    // Accumulate energy and packet counts for current cycle
    if (inDischarge_) {
        float intervalH     = intervalSec / 3600.0f;
        cycleEnergyMah_    += estimatedMa * intervalH;
        cycleTxPkts_       += deltaTx;
        cycleRelayPkts_    += deltaRelay;
        cycleRxPkts_       += deltaRx;

#ifdef HAS_GPS
        if (gpsStatus && gpsStatus->getIsConnected()) {
            cycleGpsActiveSecs_ += intervalSec;
            if (gpsStatus->getHasLock())
                cycleGpsFixSecs_ += intervalSec;
        }
#endif
    }

    lastTxTotal_    = txTotal;
    lastRelayTotal_ = relayTotal;
    lastRxTotal_    = rxTotal;
    lastUpdateSec_  = nowSec;

    // ── Save running state to NVS periodically ─────────────────────────────
    if (nowSec - lastNvsSaveSec_ >= BAT_NVS_SAVE_INTERVAL_S) {
        saveRunningStateToNvs();
        lastNvsSaveSec_ = nowSec;
    }
}

// ─── startCycle ──────────────────────────────────────────────────────────────
void BatteryTracker::startCycle(uint8_t socStart, uint32_t txBase, uint32_t relayBase, uint32_t rxBase)
{
    inDischarge_            = true;
    cycleStartSec_          = getTime() ? getTime() : millis() / 1000;
    cycleStartSoc_          = socStart;
    cycleEnergyMah_         = 0.0f;
    cycleTxBase_            = txBase;
    cycleRelayBase_         = relayBase;
    cycleRxBase_            = rxBase;
    cycleTxPkts_            = 0;
    cycleRelayPkts_         = 0;
    cycleRxPkts_            = 0;
    cycleGpsActiveSecs_     = 0;
    cycleGpsFixSecs_        = 0;
}

// ─── onCycleEnd ──────────────────────────────────────────────────────────────
void BatteryTracker::onCycleEnd(uint8_t socEnd)
{
    if (!inDischarge_)
        return;
    inDischarge_ = false;

    uint32_t nowSec      = getTime() ? getTime() : millis() / 1000;
    uint32_t durationSec = (nowSec > cycleStartSec_) ? nowSec - cycleStartSec_ : 1;
    float    socFraction = (cycleStartSoc_ > socEnd)
        ? (cycleStartSoc_ - socEnd) / 100.0f
        : 0.0f;

    // Need at least 10% SoC drop and 5min of data to be useful
    if (socFraction < 0.10f || durationSec < 300 || cycleEnergyMah_ < 1.0f) {
        return;
    }

    uint32_t cycleCapMah = (socFraction > 0.01f)
        ? (uint32_t)(cycleEnergyMah_ / socFraction)
        : 0;

    // Sanity bounds: 100 mAh – 30 000 mAh
    if (cycleCapMah < 100 || cycleCapMah > 30000)
        return;

    // EMA update (α ≈ 0.3)
    if (learnedCapacityMah_ == 0) {
        learnedCapacityMah_ = cycleCapMah;
    } else {
        learnedCapacityMah_ = (uint32_t)(0.7f * learnedCapacityMah_ + 0.3f * cycleCapMah);
    }

    // Daily average update
    float durationDays = durationSec / 86400.0f;
    float dailyEquiv   = (durationDays > 0) ? cycleEnergyMah_ / durationDays : 0;
    if (avgDailyMah_ == 0)
        avgDailyMah_ = dailyEquiv;
    else
        avgDailyMah_ = 0.7f * avgDailyMah_ + 0.3f * dailyEquiv;

    if (cycleCount_ < 255)
        cycleCount_++;

    // Build cycle record
    BatteryCycleNvs cyc = {};
    cyc.timestamp_start        = cycleStartSec_;
    cyc.duration_secs          = durationSec;
    cyc.soc_start              = cycleStartSoc_;
    cyc.soc_end                = socEnd;
    cyc.tx_packets             = cycleTxPkts_;
    cyc.tx_relay               = cycleRelayPkts_;
    cyc.rx_packets             = cycleRxPkts_;
    cyc.gps_active_secs        = cycleGpsActiveSecs_;
    cyc.gps_fix_secs           = cycleGpsFixSecs_;
    cyc.capacity_estimate_mah  = cycleCapMah;

    // Update in-RAM cache (ring buffer, slot = cycleCount-1 mod BAT_MAX_CYCLES)
    uint8_t slot = (cycleCount_ - 1) % BAT_MAX_CYCLES;
    cycles_[slot] = cyc;
    if (cyclesFilled_ < BAT_MAX_CYCLES)
        cyclesFilled_++;

    saveToFile();
}

// ─── resetHistory ────────────────────────────────────────────────────────────
void BatteryTracker::resetHistory()
{
    learnedCapacityMah_ = 0;
    chargedMah_         = 0;
    avgDailyMah_        = 0.0f;
    cycleCount_         = 0;
    inDischarge_            = false;
    cycleEnergyMah_         = 0.0f;
    cycleGpsActiveSecs_     = 0;
    cycleGpsFixSecs_        = 0;
    bufHead_                = 0;
    bufFill_                = 0;
    memset(curBuf_,  0, sizeof(curBuf_));
    memset(cycles_,  0, sizeof(cycles_));
    cyclesFilled_           = 0;

    spiLock->lock();
    if (FSCom.exists(BAT_FILENAME))
        FSCom.remove(BAT_FILENAME);
    spiLock->unlock();
}

// ─── setLowVoltProtectMins ────────────────────────────────────────────────────
void BatteryTracker::setLowVoltProtectMins(uint8_t mins)
{
    lowVoltProtectMins_ = mins;
    saveToFile();
}

// ─── Getters ─────────────────────────────────────────────────────────────────
float BatteryTracker::getAvgCurrentMa(uint32_t windowSecs) const
{
    uint8_t nSamples = (uint8_t)(windowSecs / BAT_UPDATE_INTERVAL_S);
    if (nSamples < 1) nSamples = 1;
    if (nSamples > BAT_BUF_SIZE) nSamples = BAT_BUF_SIZE;
    return getAvgFromBuf(nSamples);
}

uint32_t BatteryTracker::getCurrentMah() const
{
    if (learnedCapacityMah_ == 0)
        return 0;
    return (uint32_t)((lastSocPct_ / 100.0f) * learnedCapacityMah_);
}

uint32_t BatteryTracker::getUsedMah() const
{
    uint32_t cur = getCurrentMah();
    return (chargedMah_ >= cur) ? (chargedMah_ - cur) : 0;
}

float BatteryTracker::getEstLifeHours() const
{
    uint32_t remainMah = getCurrentMah();
    if (remainMah == 0)
        return 0.0f;
    float avgMa = getAvgFromBuf(bufFill_ > 0 ? bufFill_ : 1);
    if (avgMa < 1.0f)
        return 0.0f;
    return remainMah / avgMa;
}

const BatteryCycleNvs* BatteryTracker::getCycle(uint8_t idx) const
{
    if (idx >= cyclesFilled_)
        return nullptr;
    // Map idx (0=oldest) to the ring buffer slot.
    // The ring buffer writes into slot (cycleCount_-1) % BAT_MAX_CYCLES for newest.
    // With cyclesFilled_ filled slots, oldest is at:
    //   (cycleCount_ - cyclesFilled_) % BAT_MAX_CYCLES
    uint8_t oldest = (cycleCount_ - cyclesFilled_) % BAT_MAX_CYCLES;
    uint8_t slot   = (oldest + idx) % BAT_MAX_CYCLES;
    return &cycles_[slot];
}

uint32_t BatteryTracker::getCurrentCycleDurationSecs() const
{
    if (!inDischarge_ || cycleStartSec_ == 0)
        return 0;
    uint32_t nowSec = getTime();
    if (nowSec == 0)
        nowSec = millis() / 1000;
    return (nowSec > cycleStartSec_) ? nowSec - cycleStartSec_ : 0;
}

// ─── Private helpers ─────────────────────────────────────────────────────────
float BatteryTracker::estimateCurrentMa(uint32_t deltaTx, uint32_t deltaRx) const
{
    float current = BAT_BASE_CURRENT_MA;

    // TX energy: each packet draws TX_CURRENT for TX_DURATION seconds
    if (deltaTx > 0) {
        float txEnergyFraction = (BAT_TX_CURRENT_MA * BAT_TX_DURATION_S)
                                  / (float)BAT_UPDATE_INTERVAL_S;
        current += deltaTx * txEnergyFraction;
    }
    // RX doesn't add significant extra current vs base (LoRa RX is in base)
    (void)deltaRx;

#ifdef HAS_GPS
    if (gpsStatus && gpsStatus->getIsConnected()) {
        current += BAT_GPS_CURRENT_MA;
    }
#endif

#if defined(HAS_WIFI) && defined(ARCH_ESP32)
    if (WiFi.status() == WL_CONNECTED) {
        current += BAT_WIFI_CURRENT_MA;
    }
#endif

    return current;
}

void BatteryTracker::pushCurrent(float mA)
{
    curBuf_[bufHead_] = mA;
    bufHead_ = (bufHead_ + 1) % BAT_BUF_SIZE;
    if (bufFill_ < BAT_BUF_SIZE)
        bufFill_++;
}

float BatteryTracker::getAvgFromBuf(uint8_t nSamples) const
{
    if (bufFill_ == 0 || nSamples == 0)
        return BAT_BASE_CURRENT_MA; // fallback

    if (nSamples > bufFill_)
        nSamples = bufFill_;

    float sum = 0.0f;
    for (uint8_t i = 0; i < nSamples; i++) {
        uint8_t idx = (bufHead_ + BAT_BUF_SIZE - 1 - i) % BAT_BUF_SIZE;
        sum += curBuf_[idx];
    }
    return sum / nSamples;
}

// ─── File storage ─────────────────────────────────────────────────────────────
void BatteryTracker::loadFromNvs()
{
    spiLock->lock();
    auto file = FSCom.open(BAT_FILENAME, FILE_O_READ);
    if (!file) {
        spiLock->unlock();
        return;
    }
    BatteryFileV2 f = {};
    bool ok = (file.read((uint8_t *)&f, sizeof(f)) == sizeof(f));
    file.close();
    spiLock->unlock();

    if (!ok || f.magic != BAT_FILE_MAGIC)
        return;

    learnedCapacityMah_ = f.learnedCapacityMah;
    chargedMah_         = f.chargedMah;
    cycleCount_         = f.cycleCount;
    cyclesFilled_       = f.cyclesFilled;
    avgDailyMah_        = f.avgDailyMah;
    lowVoltProtectMins_ = f.lvProtectMins;
    memcpy(cycles_, f.cycles, sizeof(cycles_));
}

void BatteryTracker::saveRunningStateToNvs()
{
    saveToFile();
}

void BatteryTracker::saveToFile()
{
    BatteryFileV2 f = {};
    f.magic              = BAT_FILE_MAGIC;
    f.learnedCapacityMah = learnedCapacityMah_;
    f.chargedMah         = chargedMah_;
    f.cycleCount         = cycleCount_;
    f.cyclesFilled       = cyclesFilled_;
    f.avgDailyMah        = avgDailyMah_;
    f.lvProtectMins      = lowVoltProtectMins_;
    memcpy(f.cycles, cycles_, sizeof(cycles_));

    spiLock->lock();
    FSCom.mkdir("/prefs");
    if (FSCom.exists(BAT_FILENAME))
        FSCom.remove(BAT_FILENAME);
    auto file = FSCom.open(BAT_FILENAME, FILE_O_WRITE);
    if (file) {
        file.write((uint8_t *)&f, sizeof(f));
        file.flush();
        file.close();
    }
    spiLock->unlock();
}
