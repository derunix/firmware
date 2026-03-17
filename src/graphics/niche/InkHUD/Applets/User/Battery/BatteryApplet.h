#ifdef MESHTASTIC_INCLUDE_INKHUD

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

class BatteryApplet : public Applet
{
  public:
    BatteryApplet() { name = "Battery"; }
    void onRender(bool full) override;
};

} // namespace NicheGraphics::InkHUD

#endif
