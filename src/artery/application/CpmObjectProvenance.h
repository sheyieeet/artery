#ifndef ARTERY_CPM_OBJECT_PROVENANCE_H
#define ARTERY_CPM_OBJECT_PROVENANCE_H

#include "artery/application/CpObject.h"
#include <omnetpp/simtime.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace omnetpp { class cComponent; }

namespace artery {
class EnvironmentModelObject;

// Simulation-only provenance. Neither this identity nor this timestamp is
// serialized. A weak owner identifies one ground-truth object lifetime, even
// when a SUMO name, reporting station ID, or CPM object ID is reused.
struct CpmObjectProvenance {
    std::weak_ptr<EnvironmentModelObject> object;
    omnetpp::SimTime informationTime;
    bool known = false;
};

void publishCpmObjectProvenance(const std::shared_ptr<const Cpm>&,
    std::vector<CpmObjectProvenance>);
bool cpmObjectFreshnessEnabled();
void observeCpmObjectFreshness(omnetpp::cComponent*, const CpObject&);
CpmObjectProvenance lookupCpmObjectProvenance(const std::shared_ptr<const Cpm>&,
    const Vanetza_ITS2_PerceivedObject_t*);

// Mirror the encoder's ordered selection and 255-entry cap. Metadata is aligned
// with actual POC entries; CPM object IDs are not used for physical matching.
template<class Snapshot>
void attachCpmObjectProvenance(const std::shared_ptr<const Cpm>& message,
    const std::vector<Snapshot>& snapshots, const std::vector<std::size_t>& selected)
{
    if (!cpmObjectFreshnessEnabled()) return;
    std::vector<CpmObjectProvenance> provenance;
    const auto count = std::min(selected.size(), std::size_t(255));
    provenance.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (selected[i] < snapshots.size()) provenance.push_back(snapshots[selected[i]].provenance);
    }
    publishCpmObjectProvenance(message, std::move(provenance));
}
} // namespace artery
#endif
