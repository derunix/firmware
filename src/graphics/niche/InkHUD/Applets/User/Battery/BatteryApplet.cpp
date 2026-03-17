#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./BatteryApplet.h"
#include "power/BatteryTracker.h"

#include <inttypes.h>
#include <stdio.h>

using namespace NicheGraphics;

void InkHUD::BatteryApplet::onRender(bool full)
{
    const BatteryTracker *bt = BatteryTracker::instance();

    drawHeader("Battery Info");

    setFont(fontSmall);

    const uint16_t hh = getHeaderHeight();
    const uint16_t lineH = fontSmall.lineHeight();
    const uint16_t pad = 3;
    const uint16_t col2 = X(0.5f);

    uint16_t y = hh + pad;

    char buf[32];

    // Row 1: Voltage | Current
    snprintf(buf, sizeof(buf), "Voltage:  %.3f V", bt->getVoltageV());
    printAt(0, y, buf);
    snprintf(buf, sizeof(buf), "Current: ~%.0f mA", bt->getAvgCurrentMa(BAT_UPDATE_INTERVAL_S));
    printAt(col2, y, buf);
    y += lineH;

    // Row 2: Avg 1m | Avg 5m
    snprintf(buf, sizeof(buf), "Avg  1m:  %.0f mA", bt->getAvgCurrentMa(60));
    printAt(0, y, buf);
    snprintf(buf, sizeof(buf), "Avg  5m:  %.0f mA", bt->getAvgCurrentMa(300));
    printAt(col2, y, buf);
    y += lineH;

    // Row 3: Avg 15m | Avg/day
    snprintf(buf, sizeof(buf), "Avg 15m:  %.0f mA", bt->getAvgCurrentMa(900));
    printAt(0, y, buf);
    float daily = bt->getAvgDailyMah();
    if (daily > 0)
        snprintf(buf, sizeof(buf), "Avg/day: %.0f mAh", daily);
    else
        snprintf(buf, sizeof(buf), "Avg/day: -- mAh");
    printAt(col2, y, buf);
    y += lineH;

    // Divider
    drawLine(0, y, width(), y, BLACK);
    y += pad + 1;

    uint32_t learnedCap = bt->getLearnedCapacityMah();
    if (learnedCap == 0) {
        printAt(X(0.5f), y + lineH, "No data yet. Run 1+ full cycles.", CENTER, TOP);
        return;
    }

    // Row 4: Charged | Now
    uint32_t charged = bt->getChargedMah();
    if (charged > 0)
        snprintf(buf, sizeof(buf), "Charged: %" PRIu32 " mAh", charged);
    else
        snprintf(buf, sizeof(buf), "Charged: -- mAh");
    printAt(0, y, buf);
    snprintf(buf, sizeof(buf), "Now: %" PRIu32 " mAh  %d%%", bt->getCurrentMah(), (int)bt->getSocPct());
    printAt(col2, y, buf);
    y += lineH;

    // Row 5: Used | Est life + cycles
    snprintf(buf, sizeof(buf), "Used: %" PRIu32 " mAh", bt->getUsedMah());
    printAt(0, y, buf);
    float life = bt->getEstLifeHours();
    uint8_t cyc = bt->getCycleCount();
    if (life > 0)
        snprintf(buf, sizeof(buf), "Est: %.1fh (%d cyc)", life, (int)cyc);
    else
        snprintf(buf, sizeof(buf), "Est: --  (%d cyc)", (int)cyc);
    printAt(col2, y, buf);
}

#endif
