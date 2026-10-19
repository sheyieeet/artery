# Weighted p-persistence forwarding

The corrections are confined to `CpServiceWeightedPPersistence.{cc,h,ned}`.
Other algorithms, shared transmission accounting, link-layer PRR, ASN.1,
scenario settings, warm-up, object selection and graph scripts are unchanged.

## Approved behavior

A logical CPM is identified by the tuple of original station ID, full management
reference time and SHA-256 of the encoded CPM application payload. GN/radio
headers and physical transmission IDs are excluded. Identical copies keep the
same identity across relays; distinct bodies at the same origin/time remain
separate. Bit-identical messages with the same origin/time are necessarily
treated as the same logical message. PRR still counts individual physical
transmissions separately.

Locally generated CPMs are marked seen before submission, including the existing
selected-link generation path if the service is used on an RSU. No generation
or object-selection rules changed. The configured Road 120 and InTAS RSUs still
use the existing common `CpService`, retaining their established roles.

On first reception of an eligible CPM, a station waits `waitTime` (5 ms). During
that initial window, duplicate copies update the smallest last-hop distance;
they do not cancel the decision. Each distance uses the immediate forwarder's
GN source position and the receiver's fresh VDP coordinates, or current static
geographic position when no VDP exists. Longitude differences handle the date
line. Existing RX distance/object calculations continue to use the CPM origin.

At the initial timer, the station draws once from the existing global RNG 0:

```
p = clamp(minimumLastHopDistance / transmissionRange, 0, 1)
if uniform(globalRng0, 0, 1) < p:
    submit once; finish the decision
else:
    wait an additional tau (5 ms)
    cancel if a valid duplicate arrives during this fallback window
    otherwise submit once; finish the decision
```

The configured normalization range remains 500 m; it is not a new geographic
forwarding boundary. The strict comparison makes a zero-probability initial
trial fail even if the RNG returns exactly zero. Probability-one fallback is
still allowed. There is no additional draw for duplicates, fallback, expiry or
bookkeeping, no periodic retry, no hop cap and no retry-count cap. Different
valid message/traffic events can naturally change subsequent RNG consumption;
the fixes do not promise identical trajectories to the buggy implementation.

The new `maxCpmAge` parameter defaults to the approved 2 s absolute age from the
original CPM reference time. It includes all application relay hops and waiting
periods and is never restarted at reception. Expired and future-dated messages
are rejected from forwarding. Every timer checks expiry before drawing or
submitting. Successful link receptions and existing RX/object metrics retain
their established handling; the age policy controls forwarding eligibility.

Application payloads are retained only while a forwarding decision is pending.
Forwarding or cancellation releases that cache entry immediately, retaining a
compact seen key until the absolute deadline. A separate expiry event prunes
history even during idle periods. Old copies cannot revive a forgotten key,
because they then fail the absolute-age check. Valid out-of-order CPMs are not
discarded by an origin-wide latest-timestamp rule. State is bounded by arrivals
within one validity interval, not a fixed memory cap under arbitrary input rate.

Forwarded network requests keep the previous one-second lifetime ceiling and
are clipped to remaining original CPM validity. GN represents lifetimes in
steps of at least 50 ms, so they round down. A remaining lifetime below 50 ms
cannot produce a positive valid request and causes no submission or TX sample.
All actual requests continue through unchanged common accounting exactly once.

Finish and destruction use idempotent cleanup for pending timers, payloads and
seen records. Shared accounting still flushes its final measurement window.
There are no new production per-packet logs or random draws for diagnostics.

## Validation

With an existing CMake build, from the repository root:

```sh
cmake --build build --parallel 2
python3 tests/weighted_pp/run.py
python3 tests/weighted_pp/run.py --case zero --output /tmp/weighted-check
```

The fixtures exercise production forwarding, idle expiry and shared accounting
with controlled CPMs/geometry and a fixture-only network sink. They independently
check serialized payload sizes, GN lifetimes, submission counts and times,
payload release through weak references, and undisposed OMNeT++ objects.

Selected boundary cases use a test-only preloaded `uniform()` hook: it verifies
the global RNG 0 mapping, consumes exactly one original RNG sample, then supplies
a known boundary value. `randomStress` uses the original samples unchanged.
Preloading is needed because `opp_run` links `liboppsim` before loading test
libraries. The hook is never loaded by production or Road 120 integration runs.
The underlying test/library loading retains the verified envmod dependency
ordering; no launcher changes are required.

The 23 cases cover unique identity at a fixed origin latitude, different payloads
at the same timestamp, full timestamps, out-of-order packets, duplicates,
own-origin returns, immediate-forwarder/minimum-distance geometry, stale local
snapshots, equator/date-line/static positions, exact zero/equal/one probability
boundaries, fallback and cancellation, initial/fallback/exact-deadline expiry,
short residual validity, future timestamps, idle pruning, finish/destruction
during either phase, and sustained traffic through both phases.

Three stress cases each inject 10,000 unique CPMs at 1,000/s, plus initial
duplicates. They check exactly 10,000 draws and 10,000 submissions, at most
2,001 retained identities, at most 12 pending payloads, and zero retained state
after quiescence. These bounds follow this test's arrival rate and timing.

Real-radio integration uses the existing accounting and PRR runners:

```sh
python3 tests/cpm_accounting/run.py --method wPPersistence
python3 tests/cpm_link_prr/validate.py /tmp/artery-cpm-accounting/road120-wPPersistence
python3 tests/cpm_accounting/run.py --method Proposed
python3 tests/vpcast/run.py
```

SUMO needs access to its local TraCI socket. The scenarios enable PRR scalars
but disable application scalars. The existing accounting validator assumes any
scalar file contains all application totals; use a vector/trace-only results
view for that scenario or a test-only configuration enabling application
scalars. Preserve the original PRR scalars for independent PRR validation.
Production measurement settings do not need to change.

## Limits and paper description

The approved extra 5 ms fallback remains an approximation of `WAIT_TIME + delta`
from the reference method; delta is not explicitly modeled. Timers continue
independently of DCC release and MAC transmission. Neighbors may have queued
copies when fallback fires, and cancellation cannot recall copies already
submitted below the application. Residual GN lifetime limits new requests and
DCC queue validity; a packet already accepted by MAC can still transmit later.
No DCC-aware timing or queue recall was added.

Section V.B should state the logical identity, last-hop distance and minimum
rule, 500 m normalization, one probability draw and strict comparison, both
5 ms windows, phase-specific duplicate handling, single fallback, 2 s absolute
reference-time age, clipped GN lifetime and its 50 ms granularity, and compact
replay retention. Identify the lifetime as a CPM-specific adaptation, retain
the DCC timing limitation, and document the absence of an end-to-end hop limit.
The reference article's TTL=100 experimental setting is not adopted as a new
mandatory algorithm parameter. Document the existing vehicle/RSU roles and keep
application submissions distinct from actual-air PRR opportunities.

Re-run weighted p-persistence paper results after these changes: fixing identity
restores legitimate forwarding and can increase traffic; geometry, expiry and
own-origin replay protection can reduce other traffic. Net load, PRR, delay and
freshness must be measured rather than inferred from these short tests.
