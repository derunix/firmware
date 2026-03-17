#include "configuration.h"
#if HAS_SCREEN

#include "BatteryRenderer.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "power/BatteryTracker.h"

#ifdef HAS_GPS
#include "GPSStatus.h"
extern meshtastic::GPSStatus *gpsStatus;
#endif

#include <inttypes.h>
#include <stdio.h>

namespace graphics
{
namespace BatteryRenderer
{

// ─── Screen 1: voltage + current averages ────────────────────────────────────
void drawBatteryCurrentFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawCommonHeader(display, x, y, "Battery 1/2");

    const BatteryTracker *bt = BatteryTracker::instance();
    const int *tp            = getTextPositions(display);

    char buf[28];

    // Line 1 – Voltage
    snprintf(buf, sizeof(buf), "Voltage:  %4.3f V", bt->getVoltageV());
    display->drawString(x, tp[1], buf);

    // Line 2 – Estimated current (always estimated — no INA)
    float cur = bt->getAvgCurrentMa(BAT_UPDATE_INTERVAL_S);
    snprintf(buf, sizeof(buf), "Current: ~%4.0f mA", cur);
    display->drawString(x, tp[2], buf);

    // Line 3 – 1-min average
    snprintf(buf, sizeof(buf), "Avg  1m:  %4.0f mA", bt->getAvgCurrentMa(60));
    display->drawString(x, tp[3], buf);

    // Line 4 – 5-min average
    snprintf(buf, sizeof(buf), "Avg  5m:  %4.0f mA", bt->getAvgCurrentMa(300));
    display->drawString(x, tp[4], buf);

    // Line 5 – 15-min average
    snprintf(buf, sizeof(buf), "Avg 15m:  %4.0f mA", bt->getAvgCurrentMa(900));
    display->drawString(x, tp[5], buf);
}

// ─── Screen 2: charge level + cycle stats ────────────────────────────────────
void drawBatteryChargeFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawCommonHeader(display, x, y, "Battery 2/2");

    const BatteryTracker *bt = BatteryTracker::instance();
    const int *tp            = getTextPositions(display);

    char buf[28];

    uint32_t learnedCap = bt->getLearnedCapacityMah();

    if (learnedCap == 0) {
        // Capacity not yet learned
        display->drawString(x, tp[1], "No data yet.");
        display->drawString(x, tp[2], "Use 1+ full cycles");
        display->drawString(x, tp[3], "to calibrate.");
        display->drawString(x, tp[4], bt->getSocPct() > 0
            ? (char *)"(Run til low, charge)"
            : "");
        return;
    }

    // Line 1 – mAh at last charge disconnect
    uint32_t charged = bt->getChargedMah();
    if (charged > 0)
        snprintf(buf, sizeof(buf), "Charged: %5" PRIu32 " mAh", charged);
    else
        snprintf(buf, sizeof(buf), "Charged:    -- mAh");
    display->drawString(x, tp[1], buf);

    // Line 2 – Current remaining mAh
    uint32_t nowMah = bt->getCurrentMah();
    snprintf(buf, sizeof(buf), "Now:     %5" PRIu32 " mAh", nowMah);
    display->drawString(x, tp[2], buf);

    // Line 3 – Used mAh
    uint32_t usedMah = bt->getUsedMah();
    if (charged > 0)
        snprintf(buf, sizeof(buf), "Used:    %5" PRIu32 " mAh", usedMah);
    else
        snprintf(buf, sizeof(buf), "Used:       -- mAh");
    display->drawString(x, tp[3], buf);

    // Line 4 – Average daily consumption
    float daily = bt->getAvgDailyMah();
    if (daily > 0)
        snprintf(buf, sizeof(buf), "Avg/day: %5.0f mAh", daily);
    else
        snprintf(buf, sizeof(buf), "Avg/day:    -- mAh");
    display->drawString(x, tp[4], buf);

    // Line 5 – Estimated life + cycle count
    float life = bt->getEstLifeHours();
    uint8_t cyc = bt->getCycleCount();
    if (life > 0)
        snprintf(buf, sizeof(buf), "Est:%5.1fh (%d cyc)", life, cyc);
    else
        snprintf(buf, sizeof(buf), "Est: --  (%d cyc)", cyc);
    display->drawString(x, tp[5], buf);
}

// Helper: format a duration in seconds as "Xd Xh", "Xh Xm" or "Xm"
static void formatDuration(char *buf, size_t len, uint32_t secs)
{
    uint32_t d = secs / 86400;
    uint32_t h = (secs % 86400) / 3600;
    uint32_t m = (secs % 3600) / 60;
    if (d > 0)
        snprintf(buf, len, "%ud%uh", (unsigned)d, (unsigned)h);
    else if (h > 0)
        snprintf(buf, len, "%uh%um", (unsigned)h, (unsigned)m);
    else
        snprintf(buf, len, "%um", (unsigned)m);
}

// ─── Screen 3: packet stats — live (current cycle) + historical ───────────────
void drawStatsFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawCommonHeader(display, x, y, "Packet Stats");

    const BatteryTracker *bt = BatteryTracker::instance();
    const int *tp            = getTextPositions(display);

    char buf[24];

    // ── Line 1: live current-cycle counters ───────────────────────────────
    if (bt->isInDischarge()) {
        uint32_t sent = bt->getCurrentCycleTxPkts() >= bt->getCurrentCycleRelayPkts()
                        ? bt->getCurrentCycleTxPkts() - bt->getCurrentCycleRelayPkts() : 0;
        snprintf(buf, sizeof(buf), "Now T:%" PRIu32 " F:%" PRIu32 " R:%" PRIu32,
                 sent, bt->getCurrentCycleRelayPkts(), bt->getCurrentCycleRxPkts());
    } else {
        snprintf(buf, sizeof(buf), "Now: not discharging");
    }
    display->drawString(x, tp[1], buf);

    uint8_t filled = bt->getCyclesFilled();
    if (filled == 0) {
        display->drawString(x, tp[2], "No history yet.");
        display->drawString(x, tp[3], "Complete a charge");
        display->drawString(x, tp[4], "cycle to calibrate.");
        return;
    }

    // ── Line 2: column header ─────────────────────────────────────────────
    display->drawString(x, tp[2], "#  Snt  Fwd    Rx");

    // ── Lines 3-6: up to 4 most recent cycles ─────────────────────────────
    uint8_t show = (filled < 4) ? filled : 4;
    // Show from oldest to newest within those 4 so numbering matches
    uint8_t startIdx = (filled > 4) ? filled - 4 : 0;
    for (uint8_t i = 0; i < show; i++) {
        const BatteryCycleNvs *cyc = bt->getCycle(startIdx + i);
        if (!cyc) break;
        uint32_t sent = (cyc->tx_packets >= cyc->tx_relay) ? cyc->tx_packets - cyc->tx_relay : 0;
        snprintf(buf, sizeof(buf), "%u %4" PRIu32 " %4" PRIu32 " %5" PRIu32,
                 (unsigned)(startIdx + i + 1), sent, cyc->tx_relay, cyc->rx_packets);
        display->drawString(x, tp[3 + i], buf);
    }
}

// Helper: return current GPS state as a short string
#ifdef HAS_GPS
static const char *gpsStateStr()
{
    if (!gpsStatus)
        return "Not Present";
    if (config.position.gps_mode == meshtastic_Config_PositionConfig_GpsMode_DISABLED)
        return "Disabled";
    if (!gpsStatus->getIsConnected())
        return "Off / Error";
    if (gpsStatus->getHasLock()) {
        static char lockStr[18];
        uint32_t sats = gpsStatus->getNumSatellites();
        snprintf(lockStr, sizeof(lockStr), "Fix (%u sats)", (unsigned)sats);
        return lockStr;
    }
    return "Searching...";
}
#endif

// ─── Screen 4: GPS stats — live state + per-cycle GPS% ────────────────────────
void drawGpsStatsFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);

    drawCommonHeader(display, x, y, "GPS Stats");

    const BatteryTracker *bt = BatteryTracker::instance();
    const int *tp            = getTextPositions(display);

    char buf[24];

    // ── Line 1: live GPS state ────────────────────────────────────────────
#ifdef HAS_GPS
    snprintf(buf, sizeof(buf), "GPS: %s", gpsStateStr());
#else
    snprintf(buf, sizeof(buf), "GPS: Not present");
#endif
    display->drawString(x, tp[1], buf);

    uint8_t filled = bt->getCyclesFilled();
    if (filled == 0) {
        display->drawString(x, tp[2], "No history yet.");
        return;
    }

    // ── Line 2: column header ─────────────────────────────────────────────
    display->drawString(x, tp[2], "#  Act%  Fix%  Dur");

    // ── Lines 3-6: up to 4 most recent cycles ─────────────────────────────
    uint8_t show = (filled < 4) ? filled : 4;
    uint8_t startIdx = (filled > 4) ? filled - 4 : 0;
    for (uint8_t i = 0; i < show; i++) {
        const BatteryCycleNvs *cyc = bt->getCycle(startIdx + i);
        if (!cyc || cyc->duration_secs == 0) break;

        uint32_t actPct = cyc->gps_active_secs * 100 / cyc->duration_secs;
        uint32_t fixPct = cyc->gps_fix_secs    * 100 / cyc->duration_secs;
        char dur[10];
        formatDuration(dur, sizeof(dur), cyc->duration_secs);
        snprintf(buf, sizeof(buf), "%u %4" PRIu32 "%% %4" PRIu32 "%%  %s",
                 (unsigned)(startIdx + i + 1), actPct, fixPct, dur);
        display->drawString(x, tp[3 + i], buf);
    }
}

} // namespace BatteryRenderer
} // namespace graphics

#endif // HAS_SCREEN
