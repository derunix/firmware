#ifdef ESP32
#include "WifiScanner.h"
#include "configuration.h"
#include <WiFi.h>
#include <algorithm>
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
        WiFi.mode(WIFI_OFF);
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

    // ── Check if a running scan has finished ─────────────────────────────────
    if (scanning_) {
        int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) {
            if (now - scanStartMs_ > WIFI_GEO_SCAN_TIMEOUT_MS) {
                LOG_WARN("WifiScanner: scan timed out, aborting\n");
                WiFi.scanDelete();
                scanning_ = false;
                if (wifiWasOff_) {
                    WiFi.mode(WIFI_OFF);
                    wifiWasOff_ = false;
                }
                nextScanMs_ = now + WIFI_GEO_SCAN_INTERVAL_MS;
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
    nextScanMs_ = now + WIFI_GEO_SCAN_INTERVAL_MS;
    startAsyncScan();
    return 500; // first check-back
}

// ─── private implementation ──────────────────────────────────────────────────

void WifiScanner::startAsyncScan()
{
    WiFiMode_t mode = WiFi.getMode();
    wifiWasOff_ = false;

    if (mode == WIFI_OFF) {
#if WIFI_GEO_SCAN_WHEN_RADIO_OFF
        LOG_DEBUG("WifiScanner: WiFi was OFF, enabling STA for scan\n");
        WiFi.mode(WIFI_STA);
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
        WiFi.mode(WIFI_OFF);
        wifiWasOff_ = false;
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

} // namespace position
#endif // ESP32
