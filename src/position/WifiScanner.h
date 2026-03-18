#pragma once
#ifdef ESP32

#include "PositionConfig.h"
#include "PositionTypes.h"
#include "concurrency/OSThread.h"
#include <stdint.h>

namespace position {

// Asynchronously scans for Wi-Fi access points without connecting to any.
// Manages WiFi mode carefully: if WiFi was already active the existing mode
// is preserved; if it was OFF and WIFI_GEO_SCAN_WHEN_RADIO_OFF is set the
// radio is briefly enabled in STA mode, then restored after the scan.
//
// Results are sorted by RSSI and limited to the top WIFI_GEO_SCAN_TOP_N APs.
// Call scanComplete() to check whether new results are available.
class WifiScanner : private concurrency::OSThread {
  public:
    WifiScanner();
    ~WifiScanner();

    // Returns true if a scan has finished since the last call to takeResults().
    bool scanComplete() const { return ready_; }

    // Copy the current result set into `out` (up to maxCount entries).
    // Returns the number of entries written and resets the ready flag.
    // NOT thread-safe with runOnce(); call only from the main thread.
    uint8_t takeResults(WifiObservation *out, uint8_t maxCount);

    // Number of APs captured in the last completed scan.
    uint8_t lastCount() const { return count_; }

    // Force an immediate scan on the next runOnce() tick.
    void triggerScan() { forceScan_ = true; }

  protected:
    int32_t runOnce() override;

  private:
    void startAsyncScan();
    void collectAsyncResults();
    static uint8_t bssidToBytes(int networkIdx, uint8_t out[6]);

    WifiObservation buf_[WIFI_GEO_SCAN_TOP_N]; ///< result buffer
    uint8_t         count_       = 0;
    bool            ready_       = false;
    bool            scanning_    = false;
    bool            forceScan_   = false;
    bool            wifiWasOff_  = false;   ///< we enabled WiFi; must restore
    uint32_t        scanStartMs_ = 0;
    uint32_t        nextScanMs_  = 0;
};

} // namespace position
#endif // ESP32
