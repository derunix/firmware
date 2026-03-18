#pragma once
#if defined(ESP32) && ENABLE_REMOTE_WIFI_GEO

#include "PositionConfig.h"
#include "PositionTypes.h"
#include <stdint.h>

namespace position {

// HTTP client for external Wi-Fi geolocation APIs (Google Geolocation API
// format).  Requires the device to be connected to the internet via WiFi.
//
// The request body format:
//   { "wifiAccessPoints": [ { "macAddress":"XX:XX:XX:XX:XX:XX",
//                             "signalStrength":-60, "channel":6 }, … ] }
//
// The expected response body (success HTTP 200):
//   { "location": { "lat": 51.0, "lng": -0.1 }, "accuracy": 500.0 }
class WifiGeoClient {
  public:
    WifiGeoClient() = default;

    // Perform a synchronous HTTP lookup.
    // `obs` must contain at least one entry.
    // Returns true and fills `out` on success.
    // Blocks for up to REMOTE_GEO_TIMEOUT_MS milliseconds.
    bool lookup(const WifiObservation *obs, uint8_t obsCount,
                PositionEstimate &out);

  private:
    // Build the JSON request body into buf (null-terminated).
    // Returns the number of bytes written (not counting the null).
    static size_t buildRequestBody(const WifiObservation *obs, uint8_t count,
                                   char *buf, size_t bufLen);

    // Extract lat/lng/accuracy from a Google-format JSON response.
    // Returns false if parsing fails.
    static bool parseResponse(const char *body, double &lat, double &lng,
                               double &accuracy);

    // Simple double extractor: find key in JSON string and parse the value.
    static bool extractDouble(const char *json, const char *key, double &val);
};

} // namespace position
#endif // defined(ESP32) && ENABLE_REMOTE_WIFI_GEO
