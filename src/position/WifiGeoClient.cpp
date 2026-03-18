#if defined(ESP32) && ENABLE_REMOTE_WIFI_GEO
#include "WifiGeoClient.h"
#include "RTC.h"
#include "configuration.h"
#include <WiFiClient.h>
#if __has_include(<HTTPClient.h>)
#include <HTTPClient.h>
#define HAS_HTTP_CLIENT 1
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace position {

// ─── request building ─────────────────────────────────────────────────────────

size_t WifiGeoClient::buildRequestBody(const WifiObservation *obs, uint8_t count,
                                        char *buf, size_t bufLen)
{
    size_t pos = 0;
    auto append = [&](const char *s) {
        size_t l = strlen(s);
        if (pos + l < bufLen) { memcpy(buf + pos, s, l); pos += l; }
    };
    append("{\"wifiAccessPoints\":[");
    for (uint8_t i = 0; i < count; ++i) {
        const WifiObservation &o = obs[i];
        if (i > 0) append(",");
        char entry[128];
        snprintf(entry, sizeof(entry),
                 "{\"macAddress\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                 "\"signalStrength\":%d,\"channel\":%u}",
                 o.bssid[0], o.bssid[1], o.bssid[2],
                 o.bssid[3], o.bssid[4], o.bssid[5],
                 (int)o.rssi, (unsigned)o.channel);
        append(entry);
    }
    append("]}");
    if (pos < bufLen) buf[pos] = '\0';
    return pos;
}

// ─── response parsing ─────────────────────────────────────────────────────────

bool WifiGeoClient::extractDouble(const char *json, const char *key, double &val)
{
    const char *p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    // Skip whitespace and the colon.
    while (*p == ' ' || *p == ':' || *p == '"') ++p;
    char *end = nullptr;
    val = strtod(p, &end);
    return end != p;
}

bool WifiGeoClient::parseResponse(const char *body, double &lat, double &lng,
                                   double &accuracy)
{
    if (!body) return false;
    // Find "location" object then "lat" and "lng" inside it.
    const char *loc = strstr(body, "\"location\"");
    if (!loc) return false;
    // Restrict parsing to the location block (crude but reliable for known format).
    const char *blockEnd = strchr(loc, '}');
    char block[256] = {};
    if (blockEnd) {
        size_t len = (size_t)(blockEnd - loc);
        if (len >= sizeof(block)) len = sizeof(block) - 1;
        memcpy(block, loc, len);
    } else {
        strncpy(block, loc, sizeof(block) - 1);
    }

    if (!extractDouble(block, "\"lat\"", lat)) return false;
    if (!extractDouble(block, "\"lng\"", lng)) return false;
    // accuracy is at the top level, not inside location block.
    if (!extractDouble(body, "\"accuracy\"", accuracy)) accuracy = 1000.0;
    return true;
}

// ─── public lookup ────────────────────────────────────────────────────────────

bool WifiGeoClient::lookup(const WifiObservation *obs, uint8_t obsCount,
                            PositionEstimate &out)
{
#if !defined(HAS_HTTP_CLIENT)
    LOG_WARN("WifiGeoClient: HTTPClient.h not available on this platform\n");
    return false;
#else
    if (WiFi.status() != WL_CONNECTED) {
        LOG_DEBUG("WifiGeoClient: WiFi not connected, skipping remote lookup\n");
        return false;
    }

    // Build URL with optional API key.
    char url[256];
    const char *key = REMOTE_GEO_API_KEY;
    if (key[0]) {
        snprintf(url, sizeof(url), "%s?key=%s", REMOTE_GEO_URL, key);
    } else {
        snprintf(url, sizeof(url), "%s", REMOTE_GEO_URL);
    }

    // Build request body.
    char body[1024];
    size_t bodyLen = buildRequestBody(obs, obsCount, body, sizeof(body));
    if (bodyLen == 0) {
        LOG_ERROR("WifiGeoClient: failed to build request body\n");
        return false;
    }
    LOG_DEBUG("WifiGeoClient: POST %s  body_len=%u\n", url, (unsigned)bodyLen);

    HTTPClient http;
    http.setTimeout((int)REMOTE_GEO_TIMEOUT_MS);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST((uint8_t *)body, bodyLen);
    if (code != 200) {
        LOG_WARN("WifiGeoClient: HTTP %d from geo API\n", code);
        http.end();
        return false;
    }

    // Read response body (limit 512 bytes to avoid heap overflow).
    char resp[512] = {};
    WiFiClient *stream = http.getStreamPtr();
    if (stream) {
        size_t avail = (size_t)http.getSize();
        if (avail == 0 || avail > sizeof(resp) - 1) avail = sizeof(resp) - 1;
        size_t got = stream->readBytes(resp, avail);
        resp[got] = '\0';
    }
    http.end();

    double lat = 0.0, lng = 0.0, acc = 1000.0;
    if (!parseResponse(resp, lat, lng, acc)) {
        LOG_WARN("WifiGeoClient: failed to parse response: %.200s\n", resp);
        return false;
    }

    out.lat_i       = (int32_t)(lat * 1e7);
    out.lon_i       = (int32_t)(lng * 1e7);
    out.altitude_m  = 0;
    out.accuracy_m  = (uint32_t)(acc < 65535.0 ? acc : 65535.0);
    out.confidence  = acc < 100.0 ? 90 : acc < 500.0 ? 70 : 40;
    out.source      = PositionSource::RemoteWifi;
    out.used_bssids = obsCount;
    out.timestamp_sec = (uint32_t)getTime();

    LOG_INFO("WifiGeoClient: lat=%.6f lng=%.6f acc=%.0fm\n", lat, lng, acc);
    return true;
#endif // HAS_HTTP_CLIENT
}

} // namespace position
#endif // defined(ESP32) && ENABLE_REMOTE_WIFI_GEO
