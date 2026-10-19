// Test-only aggregate RNG count: read at finish, never draw from an RNG.
#include "artery/application/CpmObjectFreshnessObserver.h"
#include <omnetpp.h>
#include <dlfcn.h>

namespace artery {
void CpmObjectFreshnessObserver::finish()
{
    using Function = void(*)(CpmObjectFreshnessObserver*);
    static auto original = reinterpret_cast<Function>(dlsym(RTLD_NEXT,
        "_ZN6artery26CpmObjectFreshnessObserver6finishEv"));
    if (!original) throw omnetpp::cRuntimeError("Cannot find production freshness finish for overhead probe");
    original(this);
    recordScalar("testGlobalRngDraws", omnetpp::getEnvir()->getRNG(0)->getNumbersDrawn());
}
}
