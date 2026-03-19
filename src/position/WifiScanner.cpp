#ifdef ESP32
#include "WifiScanner.h"
#include "configuration.h"
#include <WiFi.h>
#include <algorithm>
#include <esp_bt.h>
#include <esp_task_wdt.h>
#include <string.h>

namespace position {

// ─── helpers ─────────────────────────────────────────────────────────────────

static inline uint32_t msNow() { return (uint32_t)millis(); }

// RSSI comparator: higher (less negative) first.
static bool rssiDesc(const WifiObservation &a, const WifiObservation &b)
{
    return a.rssi > b.rssi;
}

// ─── construction ────────────────────────────────────────────────────────────

WifiScanner::WifiScanner()
    : OSThread("WifiScanner", 0 /* stack default */)
{
    // First run immediately.
    setInterval(100);
}

WifiScanner::~WifiScanner()
{
    if (scanning_) {
        WiFi.scanDelete();
    }
    if (wifiWasOff_) {
        esp_task_wdt_reset();
        WiFi.mode(WIFI_OFF);
        esp_task_wdt_reset();
    }
}

// ─── public API ──────────────────────────────────────────────────────────────

uint8_t WifiScanner::takeResults(WifiObservation *out, uint8_t maxCount)
{
    if (!ready_ || maxCount == 0) return 0;
    uint8_t n = count_ < maxCount ? count_ : maxCount;
    memcpy(out, buf_, n * sizeof(WifiObservation));
    ready_ = false;
    return n;
}

// ─── OSThread ────────────────────────────────────────────────────────────────

int32_t WifiScanner::runOnce()
{
    uint32_t now = msNow();

    // ── Retry deferred WiFi.mode(OFF) once BLE is no longer holding the radio ─
    if (pendingModeOff_) {
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
            return 500; // BLE still busy, check again soon
        }
        esp_task_wdt_reset();
        WiFi.mode(WIFI_OFF);
        esp_task_wdt_reset();
        wifiWasOff_     = false;
        pendingModeOff_ = false;
        LOG_DEBUG("WifiScanner: deferred WiFi.mode(OFF) complete\n");
    }

    // ── Check if a running scan has finished ─────────────────────────────────
    if (scanning_) {
        int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) {
            if (now - scanStartMs_ > WIFI_GEO_SCAN_TIMEOUT_MS) {
                LOG_WARN("WifiScanner: scan timed out, aborting\n");
                WiFi.scanDelete();
                scanning_ = false;
                if (wifiWasOff_) {
                    esp_task_wdt_reset();
                    WiFi.mode(WIFI_OFF);
                    esp_task_wdt_reset();
                    wifiWasOff_ = false;
                }
                nextScanMs_ = now + scanIntervalMs_;
            }
            return 500; // check again in 500 ms
        }
        collectAsyncResults();
        return (int32_t)(nextScanMs_ > now ? nextScanMs_ - now : 100);
    }

    // ── Start a new scan if it is time ────────────────────────────────────────
    bool due = forceScan_ || (now >= nextScanMs_);
    if (!due) {
        return (int32_t)(nextScanMs_ - now);
    }
    forceScan_  = false;
    nextScanMs_ = now + scanIntervalMs_;
    startAsyncScan();
    return 500; // first check-back
}

// ─── private implementation ──────────────────────────────────────────────────

void WifiScanner::startAsyncScan()
{
    // Skip scan if BLE is initializing or running: WiFi.mode() transitions
    // stall while the BLE stack holds the shared radio coexistence lock,
    // which is long enough to trip the Task Watchdog Timer.
    // isActive() is false during NimBLE init (bleServer not yet created), so
    // check the raw BT controller status which covers the full init window.
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
        LOG_DEBUG("WifiScanner: BLE busy (status=%d), deferring scan\n",
                  (int)esp_bt_controller_get_status());
        nextScanMs_ = (uint32_t)millis() + 5000; // retry in 5 s
        return;
    }

    WiFiMode_t mode = WiFi.getMode();
    wifiWasOff_ = false;

    if (mode == WIFI_OFF) {
#if WIFI_GEO_SCAN_WHEN_RADIO_OFF
        LOG_DEBUG("WifiScanner: WiFi was OFF, enabling STA for scan\n");
        // WiFi.mode() is a blocking call that can take 1-3 s — reset WDT first.
        esp_task_wdt_reset();
        WiFi.mode(WIFI_STA);
        esp_task_wdt_reset();
        wifiWasOff_ = true;
#else
        LOG_DEBUG("WifiScanner: WiFi is OFF, skipping scan\n");
        return;
#endif
    }

    // async=true, show_hidden=false, passive=false (active scan is faster)
    WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/false, /*passive=*/false);
    scanning_    = true;
    scanStartMs_ = (uint32_t)millis();
    LOG_DEBUG("WifiScanner: async scan started\n");
}

void WifiScanner::collectAsyncResults()
{
    int16_t n = WiFi.scanComplete();
    scanning_ = false;

    if (wifiWasOff_) {
        // WiFi.mode(WIFI_OFF) is blocking and stalls when BLE is using the
        // shared radio coexistence lock.  Defer the mode-off if BLE is active.
        if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
            LOG_DEBUG("WifiScanner: BLE busy, deferring WiFi.mode(OFF)\n");
            // Leave wifiWasOff_ true; runOnce will retry via pendingModeOff_.
            pendingModeOff_ = true;
        } else {
            esp_task_wdt_reset();
            WiFi.mode(WIFI_OFF);
            esp_task_wdt_reset();
            wifiWasOff_ = false;
        }
    }

    if (n < 0) {
        LOG_WARN("WifiScanner: scan failed (status=%d)\n", (int)n);
        WiFi.scanDelete();
        return;
    }

    LOG_DEBUG("WifiScanner: scan done, %d AP(s) visible\n", (int)n);

    // Build temporary list (heap) to sort before selecting top-N.
    // We allocate only what we need and immediately release it.
    const int raw = n < 64 ? n : 64; // sanity cap
    WifiObservation *tmp = new WifiObservation[raw];
    if (!tmp) {
        LOG_ERROR("WifiScanner: OOM building scan result\n");
        WiFi.scanDelete();
        return;
    }

    // Deduplicate by BSSID (keep strongest RSSI per BSSID).
    int unique = 0;
    for (int i = 0; i < raw; ++i) {
        uint8_t mac[6];
        bssidToBytes(i, mac);
        int8_t rssi = (int8_t)WiFi.RSSI(i);

        // Check if already in tmp list.
        bool dup = false;
        for (int j = 0; j < unique; ++j) {
            if (memcmp(tmp[j].bssid, mac, 6) == 0) {
                if (rssi > tmp[j].rssi) tmp[j].rssi = rssi;
                dup = true;
                break;
            }
        }
        if (!dup) {
            memcpy(tmp[unique].bssid, mac, 6);
            tmp[unique].rssi    = rssi;
            tmp[unique].channel = (uint8_t)WiFi.channel(i);
            // Copy SSID for debugging only.
            String ssid = WiFi.SSID(i);
            strncpy(tmp[unique].ssid, ssid.c_str(), sizeof(tmp[unique].ssid) - 1);
            tmp[unique].ssid[sizeof(tmp[unique].ssid) - 1] = '\0';
            ++unique;
        }
    }

    WiFi.scanDelete();

    // Sort by RSSI descending and keep top-N.
    std::sort(tmp, tmp + unique, rssiDesc);
    count_ = (uint8_t)(unique < (int)WIFI_GEO_SCAN_TOP_N ? unique : WIFI_GEO_SCAN_TOP_N);
    memcpy(buf_, tmp, count_ * sizeof(WifiObservation));
    delete[] tmp;

    ready_ = true;
    LOG_INFO("WifiScanner: %d unique AP(s), kept top %d\n", unique, (int)count_);
}

uint8_t WifiScanner::bssidToBytes(int networkIdx, uint8_t out[6])
{
    uint8_t *p = WiFi.BSSID(networkIdx);
    if (p) {
        memcpy(out, p, 6);
    } else {
        memset(out, 0, 6);
    }
    return 6;
}

void WifiScanner::setScanInterval(uint32_t ms)
{
    // Clamp: minimum 30 s (allow ADC / boot to settle), maximum 30 min.
    if (ms < 30000U)   ms = 30000U;
    if (ms > 1800000U) ms = 1800000U;
    if (ms == scanIntervalMs_) return;

    scanIntervalMs_ = ms;
    LOG_DEBUG("WifiScanner: scan interval -> %lu ms\n", (unsigned long)ms);

    // If the next scan was scheduled further than the new interval, pull it in.
    uint32_t now = msNow();
    if (!scanning_ && nextScanMs_ > now + ms)
        nextScanMs_ = now + ms;
}

} // namespace position
#endif // ESP32
