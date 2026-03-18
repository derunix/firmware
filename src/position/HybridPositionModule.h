#pragma once
#ifdef ESP32

#include "PositionConfig.h"
#include "PositionManager.h"
#include "WifiDb.h"
#include "WifiScanner.h"
#include "mesh/MeshModule.h"

namespace position {

// MeshModule that ties the hybrid positioning subsystem into the Meshtastic
// module framework.
//
// Responsibilities:
//   - Owns WifiDb, WifiScanner, and PositionManager instances.
//   - Broadcasts a compact HybridPosDiagPacket (portnum PRIVATE_APP) whenever
//     a new position estimate is ready.  The standard position packet is sent
//     via the existing PositionModule (which reads from nodeDB).
//   - Initialises the WifiDb from flash on startup.
class HybridPositionModule : public MeshModule {
  public:
    HybridPositionModule();
    ~HybridPositionModule();

    // Broadcast a diagnostic packet with the current position metadata.
    // Returns true if the packet was queued successfully.
    bool sendDiagnosticPacket();

  protected:
    // We do not handle incoming packets from the mesh.
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return false; }
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        return ProcessMessage::CONTINUE;
    }

  private:
    WifiDb          *db_      = nullptr;
    WifiScanner     *scanner_ = nullptr;
    PositionManager *manager_ = nullptr;
};

extern HybridPositionModule *hybridPositionModule;

} // namespace position
#endif // ESP32
