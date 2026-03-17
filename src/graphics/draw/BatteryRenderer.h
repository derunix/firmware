#pragma once
#include "configuration.h"
#if HAS_SCREEN

#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{
namespace BatteryRenderer
{

/** Screen 1: Voltage, estimated current, and 1/5/15-min averages */
void drawBatteryCurrentFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/** Screen 2: Charged mAh, remaining mAh, used, avg/day, estimated life */
void drawBatteryChargeFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/** Screen 3: Packet stats — live current-cycle counters + last 4 historical cycles */
void drawStatsFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

/** Screen 4: GPS stats — live GPS state + per-cycle active/fix % and duration */
void drawGpsStatsFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

} // namespace BatteryRenderer
} // namespace graphics

#endif // HAS_SCREEN
