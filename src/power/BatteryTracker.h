#pragma once
#include <stdint.h>

// Default current model constants (can be overridden in variant.h)
#ifndef BAT_BASE_CURRENT_MA
#define BAT_BASE_CURRENT_MA 50.0f   // ESP32-S3 idle current (CPU active, LoRa RX)
#endif
#ifndef BAT_TX_CURRENT_MA
#define BAT_TX_CURRENT_MA 120.0f    // Extra current during LoRa TX
#endif
#ifndef BAT_TX_DURATION_S
#define BAT_TX_DURATION_S 0.1f      // LoRa TX duration per packet (seconds)
#endif
#ifndef BAT_GPS_CURRENT_MA
#define BAT_GPS_CURRENT_MA 80.0f    // Extra current when GPS is active
#endif
#ifndef BAT_WIFI_CURRENT_MA
#define BAT_WIFI_CURRENT_MA 80.0f   // Extra current when WiFi is active
#endif

// Update interval in seconds — BatteryTracker ignores calls that come too fast
static constexpr uint32_t BAT_UPDATE_INTERVAL_S = 30;

// Ring buffer size: 30 × 30s = 15 min of current estimates
static constexpr uint8_t BAT_BUF_SIZE = 30;

// How many charge cycles to keep in NVS
static constexpr uint8_t BAT_MAX_CYCLES = 5;

// Persist to NVS at most once per minute
static constexpr uint32_t BAT_NVS_SAVE_INTERVAL_S = 60;

/**
 * @brief Per-cycle statistics stored in NVS and cached in RAM.
 *        Stored as a packed blob (key "cyc0".."cyc4").
 */
struct BatteryCycleNvs {
    uint32_t timestamp_start;       // Unix epoch seconds
    uint32_t duration_secs;
    uint8_t  soc_start;
    uint8_t  soc_end;
    uint32_t tx_packets;            // txGood delta (own + relay)
    uint32_t tx_relay;              // txRelay delta (forwarded, subset of tx_packets)
    uint32_t rx_packets;            // rxGood delta
    uint32_t gps_active_secs;       // Seconds GPS module was active (connected/searching/fix)
    uint32_t gps_fix_secs;          // Seconds GPS had a valid position fix
    uint32_t capacity_estimate_mah;
};

/**
 * @brief Tracks battery usage, learns capacity over charge/discharge cycles,
 *        and exposes estimated current, mAh remaining, and cycle statistics.
 *
 * No hardware current sensor is required. Current is estimated from an
 * activity model (LoRa TX rate, GPS, WiFi) combined with battery SoC rate-of-change.
 * Capacity is refined over discharge cycles via exponential moving average.
 *
 * Persistent state is stored in NVS (Preferences namespace "battery").
 * Call setup() once after Preferences/NVS is available.
 * Call update() from Power::runOnce() or Power::readPowerStatus().
 */
class BatteryTracker
{
  public:
    static BatteryTracker *instance();

    /** Must be called once after NVS is ready (after Power::setup). */
    void setup();

    /**
     * @brief Main update. Throttled internally to BAT_UPDATE_INTERVAL_S.
     *        Call from Power::runOnce().
     * @param voltV      Battery voltage in Volts
     * @param socPct     Battery state-of-charge 0-100 %
     * @param isCharging True when USB is connected and charging
     * @param txTotal    Cumulative LoRa TX good packet count (txGood)
     * @param relayTotal Cumulative LoRa relay count (txRelay, subset of txTotal)
     * @param rxTotal    Cumulative LoRa RX good packet count (rxGood)
     */
    void update(float voltV, uint8_t socPct, bool isCharging,
                uint32_t txTotal, uint32_t relayTotal, uint32_t rxTotal);

    /** Call from button menu to wipe NVS history and restart learning. */
    void resetHistory();

    // ─── Getters ──────────────────────────────────────────────────────────────
    float    getVoltageV() const { return lastVoltV_; }
    uint8_t  getSocPct() const { return lastSocPct_; }

    /**
     * @brief Estimated average current over the last `windowSecs` seconds.
     * @param windowSecs Must be 60, 300, or 900 (1 / 5 / 15 min)
     */
    float    getAvgCurrentMa(uint32_t windowSecs) const;

    /** Average daily consumption in mAh (computed from cycle history). */
    float    getAvgDailyMah() const { return avgDailyMah_; }

    /**
     * @brief Estimated remaining charge in mAh.
     * Returns 0 if capacity not yet learned.
     */
    uint32_t getCurrentMah() const;

    /**
     * @brief mAh when charger was last disconnected (start of last discharge).
     * Returns 0 if not yet recorded.
     */
    uint32_t getChargedMah() const { return chargedMah_; }

    /** Consumed mAh since last charge. */
    uint32_t getUsedMah() const;

    /** Estimated time remaining in hours. 0 if insufficient data. */
    float    getEstLifeHours() const;

    /** Learned battery capacity in mAh. 0 = not yet calibrated. */
    uint32_t getLearnedCapacityMah() const { return learnedCapacityMah_; }

    /** Number of complete discharge cycles used for capacity estimate. */
    uint8_t  getCycleCount() const { return cycleCount_; }

    /**
     * @brief Access a completed cycle by index (0 = oldest stored, cyclesFilled-1 = most recent).
     * @return Pointer to cached cycle data, or nullptr if idx >= getCyclesFilled().
     */
    const BatteryCycleNvs* getCycle(uint8_t idx) const;

    /** Number of completed cycles cached in RAM (0..BAT_MAX_CYCLES). */
    uint8_t getCyclesFilled() const { return cyclesFilled_; }

    /**
     * @brief Minutes before low-voltage deep sleep is triggered (0 = disabled).
     * Valid values: 0, 5, 10, 15, 30. Default: 10.
     * Persisted in NVS.
     */
    uint8_t getLowVoltProtectMins() const { return lowVoltProtectMins_; }
    void    setLowVoltProtectMins(uint8_t mins);

    // ─── Current (live) cycle getters ─────────────────────────────────────
    bool     isInDischarge() const { return inDischarge_; }
    uint32_t getCurrentCycleTxPkts()    const { return inDischarge_ ? cycleTxPkts_        : 0; }
    uint32_t getCurrentCycleRelayPkts() const { return inDischarge_ ? cycleRelayPkts_     : 0; }
    uint32_t getCurrentCycleRxPkts()    const { return inDischarge_ ? cycleRxPkts_        : 0; }
    uint32_t getCurrentCycleGpsActiveSecs() const { return inDischarge_ ? cycleGpsActiveSecs_ : 0; }
    uint32_t getCurrentCycleGpsFixSecs()    const { return inDischarge_ ? cycleGpsFixSecs_    : 0; }
    /** Elapsed seconds since cycle start (0 if not in discharge). */
    uint32_t getCurrentCycleDurationSecs() const;

  private:
    BatteryTracker() = default;

    static BatteryTracker inst_;
    bool     setupDone_ = false;

    // ─── Current instant state ─────────────────────────────────────────────
    float    lastVoltV_     = 0.0f;
    uint8_t  lastSocPct_    = 0;
    uint32_t lastTxTotal_   = 0;
    uint32_t lastRelayTotal_= 0;
    uint32_t lastRxTotal_   = 0;
    bool     lastCharging_  = false;
    uint32_t lastUpdateSec_ = 0; // Epoch seconds of last update

    // ─── Ring buffer for current estimates (BAT_BUF_SIZE × 30s) ───────────
    float    curBuf_[BAT_BUF_SIZE] = {};
    uint8_t  bufHead_ = 0;
    uint8_t  bufFill_ = 0; // How many valid samples in buffer

    float    getAvgFromBuf(uint8_t nSamples) const;
    void     pushCurrent(float mA);

    // ─── Discharge cycle tracking ──────────────────────────────────────────
    bool     inDischarge_           = false;
    uint32_t cycleStartSec_         = 0;
    uint8_t  cycleStartSoc_         = 0;
    float    cycleEnergyMah_        = 0.0f; // Accumulated estimated energy this cycle
    uint32_t cycleTxBase_           = 0;    // TX counter at cycle start
    uint32_t cycleRelayBase_        = 0;    // Relay counter at cycle start
    uint32_t cycleRxBase_           = 0;    // RX counter at cycle start
    uint32_t cycleTxPkts_           = 0;    // TX packets this cycle
    uint32_t cycleRelayPkts_        = 0;    // Relay packets this cycle
    uint32_t cycleRxPkts_           = 0;    // RX packets this cycle
    uint32_t cycleGpsActiveSecs_    = 0;    // GPS active seconds this cycle
    uint32_t cycleGpsFixSecs_       = 0;    // GPS fix seconds this cycle

    // ─── User settings (persisted in NVS) ─────────────────────────────────
    uint8_t  lowVoltProtectMins_ = 10; // 0=off, 5/10/15/30

    // ─── Learned persistent data ────────────────────────────────────────────
    uint32_t learnedCapacityMah_ = 0;
    uint32_t chargedMah_         = 0;  // mAh at last charger disconnect
    float    avgDailyMah_        = 0.0f;
    uint8_t  cycleCount_         = 0;  // Number of cycles used in learned capacity

    // ─── Cycle cache (loaded from NVS at startup) ──────────────────────────
    BatteryCycleNvs cycles_[BAT_MAX_CYCLES] = {};
    uint8_t         cyclesFilled_           = 0;

    // ─── File storage ──────────────────────────────────────────────────────
    uint32_t lastNvsSaveSec_ = 0;
    void     loadFromNvs();
    void     saveRunningStateToNvs();
    void     saveToFile();

    // ─── Cycle completion ──────────────────────────────────────────────────
    void onCycleEnd(uint8_t socEnd);
    void startCycle(uint8_t socStart, uint32_t txBase, uint32_t relayBase, uint32_t rxBase);

    // ─── Current estimation ────────────────────────────────────────────────
    float estimateCurrentMa(uint32_t deltaTx, uint32_t deltaRx) const;
};
