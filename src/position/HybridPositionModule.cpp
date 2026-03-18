#ifdef ESP32
#include "HybridPositionModule.h"
#include "FSCommon.h"
#include "WifiDbSeed.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/portnums.pb.h"
#include <string.h>

namespace position {

HybridPositionModule *hybridPositionModule = nullptr;

HybridPositionModule::HybridPositionModule()
    : MeshModule("HybridPosition")
{
    db_      = new WifiDb();
    scanner_ = new WifiScanner();
    manager_ = new PositionManager(db_, scanner_);

    // Load the persistent database.  FS should already be mounted by this point
    // (setupModules() is called after fsInit()).
    bool loaded = db_->load();
    if (!loaded) {
        LOG_WARN("HybridPositionModule: DB load failed, starting empty\n");
    }
    // Import seed entries if the DB was empty (first run or corrupt file).
    if (db_->count() == 0 && kSeedCount > 0) {
        db_->importSeed(&kSeedEntries[0].rec, kSeedCount);
    }
    LOG_INFO("HybridPositionModule: init OK (DB records=%u)\n", db_->count());
}

HybridPositionModule::~HybridPositionModule()
{
    delete manager_;
    delete scanner_;
    // WifiDb destructor flushes if dirty.
    delete db_;
}

bool HybridPositionModule::sendDiagnosticPacket()
{
    if (!router) return false;

    const PositionEstimate &est = manager_->best();
    HybridPosDiagPacket pkt     = {};
    pkt.source      = (uint8_t)est.source;
    pkt.confidence  = est.confidence;
    pkt.lat_i       = est.lat_i;
    pkt.lon_i       = est.lon_i;
    pkt.accuracy_m  = (uint16_t)(est.accuracy_m < 65535 ? est.accuracy_m : 65535);
    pkt.visible_aps = manager_->lastObsCount();
    pkt.db_matches  = manager_->lastDbMatches();

    // Compute a simple XOR fingerprint over all visible BSSID bytes.
    uint16_t hash = 0;
    const WifiObservation *obs = manager_->lastObs();
    for (uint8_t i = 0; i < pkt.visible_aps; ++i) {
        for (int b = 0; b < 6; ++b) {
            hash ^= (uint16_t)obs[i].bssid[b] << (b & 1 ? 8 : 0);
        }
    }
    pkt.bssid_hash = hash;

    meshtastic_MeshPacket *p = router->allocForSending();
    if (!p) {
        LOG_ERROR("HybridPositionModule: router->allocForSending() failed\n");
        return false;
    }
    p->decoded.portnum         = meshtastic_PortNum_PRIVATE_APP;
    p->decoded.payload.size    = sizeof(HybridPosDiagPacket);
    memcpy(p->decoded.payload.bytes, &pkt, sizeof(HybridPosDiagPacket));
    p->to                      = NODENUM_BROADCAST;

    service->sendToMesh(p, RX_SRC_LOCAL, true);
    LOG_DEBUG("HybridPositionModule: diagnostic packet sent (src=%u conf=%u)\n",
              pkt.source, pkt.confidence);
    return true;
}

} // namespace position
#endif // ESP32
