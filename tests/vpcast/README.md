# VP-CAST forwarding validation

The production changes are confined to `CpServiceVPCast.{cc,h,ned}` and the
VPCast-only speed-reference settings in the Road 120 and InTAS INIs. Other
algorithms, shared submission accounting, PRR, ASN.1, RSU layouts, object
selection, perception metrics and plotting are unchanged.

## Forwarding behavior

The approved CPM configuration is T1 = 2.5 ms, T2 = 1 s, absolute CPM lifetime
T3 = 2 s, and slot duration = 1 ms. T3 starts at the original CPM's full
management reference time, including the initial forwarding wait. Relays never
renew it. This is an explicit adaptation for periodic CPMs; the original paper's
ten-minute example is not used.

The initial wait follows the original equations:

```
VR = clamp((senderSpeed + receiverSpeed) / (2 * maxVehicleSpeed), 0, 1)
Smax = commRange / nominalVehicleSpacing
N = (1 - Smax) * VR + Smax
T = N * (1 - clamp(distance / commRange, 0, 1)) * slotDuration
```

`slotMax` can explicitly override Smax. The default 500 m range and 10 m nominal
spacing give 50 slots, hence at most 50 ms with the approved slot duration.
The 10 m spacing is the original paper's example, not measured traffic density.
The exposed `maxVehicleSpeed` retains a 30 m/s fallback; the scenario overrides
use their network speed ceilings: 33.33 m/s in Road 120 and 83.33 m/s in InTAS.
The latter is present on passenger-accessible motorway lanes; it is a network
ceiling, not typical vehicle speed or a calibration result.

The immediate forwarder's position comes from the received SHB source vector.
Its speed comes from the receiving interface's local GN location table, updated
by SHB processing before the application indication. The receiver uses its fresh
VDP position, heading in radians, and speed. No global speed lookup or CPM
format extension is involved. The receiver-relative backward-direction rule is
retained; no geocast region is added. RSUs keep their existing generation/relay
roles.

After the initial transmission, T1 still starts at application submission.
If no eligible duplicate cancels it, T1 expiry sends the first SCF copy
immediately; further copies are spaced by T2. A duplicate from behind cancels
pending forwarding in T, T1 or SCF. A duplicate from ahead does not cancel.
There is no retry-count cap.

Identity is the tuple of original station ID, full reference time, and SHA-256
of the encoded CPM body. Distinct bodies at the same timestamp stay separate;
identical body copies remain the same logical CPM. Cancellation and expiration
release the cached payload and forwarding timer. Compact replay keys remain
until the original absolute deadline; expired copies are then rejected by age,
so forgetting a key cannot revive it. Originated CPMs are also remembered.
An expiry timer prunes history even without further receptions. Retention is
bounded by the arrivals in one validity interval, rather than total run length.
No full packets, per-packet logs, or random draws are added.

Forwarded requests retain the previous one-second GN lifetime ceiling and also
fit the remaining CPM validity. GN represents lifetime in steps of at least
50 ms, so requests round down and a remaining lifetime below 50 ms produces no
submission. Cancellation cannot recall a request already handed to DCC or MAC;
MAC queues may transmit an earlier accepted copy later. Changing that, or
starting T1 at physical transmission, requires a separate networking design.

## Controlled tests

From the repository root with an existing build:

```sh
cmake --build build --parallel 2
python3 tests/vpcast/run.py
python3 tests/vpcast/run.py --case cancelSCF --output /tmp/vpcast-check
```

The test library runs the real production forwarding helpers and common
submission accounting. Only these fixture modules use a test network sink.
The fixture supplies a real GN location table, known position vectors, speeds,
CPM times and duplicate arrivals. It checks encoded payload size, request
lifetime, submission counts and retry spacing. No production test hooks or
launcher edits are needed. The existing envmod-first library loading procedure
is retained.

Seventeen cases cover initial forwarding, cancellation in T/T1/SCF, non-canceling
front duplicates, ambiguous old byte identities, distinct same-time CPMs,
out-of-order messages, replay before/after expiry, own-origin returns, long
initial waits, late/short/expired/future CPMs, configurable slot inputs,
cardinal-direction/radian geometry, stale serialization snapshots, idle expiry,
cleanup, departure with `finish()`, destruction without `finish()`, and sustained
history pruning. Two stress cases each inject 10,000 distinct CPMs at 1,000/s,
with immediate cancellation or uninterrupted SCF. They check at most 2,001
retained keys/active states and zero state after quiescence. Uninterrupted SCF
produces exactly 30,000 submissions: three per CPM with the approved timings,
without a retry-count cap. Weak references check payload release;
OMNeT++ undisposed-object reporting checks timer cleanup.

For real-radio regression use the existing Road 120 runners:

```sh
python3 tests/cpm_link_prr/run.py --method VPCast --until 303.1s
python3 tests/cpm_link_prr/validate.py /tmp/artery-prr-validation/road120-VPCast
python3 tests/cpm_accounting/run.py --method VPCast
python3 tests/cpm_accounting/run.py --method Proposed
```

SUMO requires access to its local TraCI socket. The current scenarios disable
application scalars while enabling PRR scalars. The accounting validator's
optional scalar checks assume all application totals are present whenever any
scalar exists; use a vector-only results view for those configurations, or enable
application scalars in a test-only configuration. Keep the original PRR scalar
results for the independent PRR validator. Do not change production recording
settings to accommodate the test harness.

## Research interpretation

Re-run VPCast results: the old run cannot be corrected by editing its statistics.
Cancellation and replay protection remove avoidable submissions; faster initial
slots and immediate SCF entry can increase participation and early submissions.
A one-second T2 reduces continuing retry frequency, while a two-second absolute
lifetime permits older information to circulate. Therefore net traffic, PRR,
delay, freshness and channel load can move in either direction. These short
tests establish implementation behavior, not a performance ranking or an
optimal parameter profile.

Update the paper's VP-CAST description to distinguish original rules from CPM
adaptations: T1 is 2.5 **milliseconds**, the velocity denominator is **2 Vmax**,
and this implementation uses the approved 2 s absolute age and 1 ms slots.
Document the speed references, slot budget, application-submission T1 trigger
under DCC, and absence of a new geocast/region-of-interest rule.
