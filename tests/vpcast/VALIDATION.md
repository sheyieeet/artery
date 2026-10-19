# VP-CAST corrections: validation record

Validated on 2026-10-19 against the existing CMake build. Detailed run artifacts
and the production-only patch are in `/tmp/artery-vpcast-fixes/`.

## Scope

Changed production files:

- `src/artery/application/CpServiceVPCast.cc`: forwarding state machine,
  duplicate cancellation, identity, absolute expiry, hop geometry/speed,
  residual network lifetime and cleanup.
- `src/artery/application/CpServiceVPCast.h`: typed message keys/timers,
  forwarding history and helper declarations; receive callback now receives
  the existing network-interface argument.
- `src/artery/application/CpServiceVPCast.ned`: approved T2 = 1 s, absolute
  T3 = 2 s, slot duration = 1 ms; configurable spacing, slots and speed.
  T1 remains 2.5 ms.
- `scenarios/road120/omnetpp.ini`: VPCast-only Vmax = 33.33 m/s.
- `scenarios/intas/omnetpp.ini`: VPCast-only Vmax = 83.33 m/s, the configured
  network ceiling, including passenger-accessible motorway lanes.

Added `tests/vpcast/{Fixture.cc,Fixture.ned,fixture.ini,run.py,README.md}` and
this validation record. No test instrumentation is linked into production.

SHA-256 comparison with snapshots taken before these changes found exactly
these five modified production files. The other 39 protected files, including
all other algorithm sources, `CpmService`, the link PRR observer, application
CMake file and both RSU XML files, were unchanged. Road 120 retains seven RSUs;
InTAS retains 109. No launcher, ASN.1, object-selection, perception, warm-up,
aggregation or graph changes were made. InTAS was not simulated in this task.

## Build and controlled tests

`cmake --build build --parallel 2`: passed. Existing Boost deprecation notices
remain. `git diff --check`: passed.

All 17 controlled cases passed:

| Cases | Verified behavior |
| --- | --- |
| forward, front | Initial T, immediate SCF submission at T1 expiry, one-second retries; front duplicates do not cancel |
| cancelT, cancelT1, cancelSCF | Eligible duplicates cancel each phase; no restart before or after replay-history expiry |
| identity | Full timestamp decoding, old `[1,23]`/`[12,3]` byte-concatenation collision, distinct bodies at one timestamp, valid out-of-order CPMs |
| own | Returning originated CPM does not start forwarding |
| expiry, late, short, expired, future | Initial wait cannot exceed validity; original age survives relaying; no expired/future submissions; GN lifetime fits remaining age, including the 50 ms granularity limit |
| configured | Both speeds, factor-two denominator, range/spacing slot budget, speed ceiling and slot duration affect production waiting time |
| cleanup, departure | Cancel/finish/destruction release cached payloads and timers; deleting a live module without `finish()` is safe |
| stress | 10,000 distinct arrivals at 1,000/s with immediate cancellation: zero submissions, peak 2,000 replay keys, peak one active cache entry; zero retained state after quiescence |
| carryStress | 10,000 uncanceled arrivals at 1,000/s: exactly 30,000 submissions, peaks of 2,000 active states and 2,000 replay keys; zero retained state after quiescence |

All fixtures additionally check cardinal/radian geometry, fresh VDP data despite
a stale serialization snapshot, encoded payload sizes, valid GN request
lifetimes and common submission totals. Weak pointers check payload release;
OMNeT++ reports no undisposed fixture objects. These checks establish bounded
time-based retention, not a process-RSS benchmark or a fixed memory cap under
arbitrarily high input rates.

## Road 120 integration

Real headless SUMO/INET runs used the verified envmod-first library-loading
procedure. All reached their time limit and finished without simulation errors.
All seven RSUs had active CPM services; vehicle/RSU PRR role and distance totals
passed conservation checks.

| Run | Interval | Application submissions | Forwarded submissions | Actual air transmissions |
| --- | --- | ---: | ---: | ---: |
| VPCast, real-radio integration | 300–303.1 s | 67,958 | Not separately traced | 7,382 |
| VPCast, accounting regression | 300–302.1 s | 36,382 | 33,724 | 4,311 |
| Proposed, accounting regression | 300–302.1 s | 1,916 | 0 | 1,854 |

The VPCast accounting regression independently decoded each submitted wire
payload and matched recorded sizes, object counts, submission counts and
one-second window bytes/durations. It observed 532 windows, including two zero
windows, and conserved 4,177,322 application bytes across 178 station services.

The Proposed regression retained 100 ms decisions, cleared received history,
and included all locally detected objects whenever it submitted. It suppressed
909 decisions: 799 redundant-object and 110 empty-object decisions. Suppressed
decisions generated no submission or byte samples. Its 566 accounting windows
included 112 zero windows and conserved 252,719 bytes across 189 services.

Application scalar output is disabled by the existing scenario, whereas PRR
scalar output is enabled. The existing accounting validator interprets any
scalar file as enabling all application scalar totals, so its optional scalar
checks cannot run against that mixed configuration. Accounting validation used
a separate symlinked vector/trace-only view of the original output; original
PRR scalar files were independently checked by the PRR validator. No measurement
code or production recording settings were changed to accommodate this harness
limitation. Controlled fixtures separately verify application submission totals.

## Remaining limits and interpretation

T1 continues to start at application submission. DCC may not yet have released
the first copy when the 2.5 ms T1 expires, so early SCF submissions can still
accumulate. The application cannot withdraw copies already submitted to lower
layers. Forwarded requests now carry a residual lifetime that does not exceed
the original CPM deadline, rounded down to GN's 50 ms granularity and capped at
the previous one-second per-request lifetime. A copy already accepted by MAC
can still transmit later; no queue recall or physical-transmission T1 trigger
was introduced. Those would be separate cross-layer design decisions.

The large submission/air gap is therefore still possible and does not, by
itself, demonstrate replay or infinite application forwarding. The controlled
uncanceled test verifies the finite retry sequence with the approved settings.
Bit-identical CPM bodies from the same station at the same reference time are
necessarily treated as one logical message; physical rebroadcasts remain
distinct PRR opportunities through the existing observer.

The approved settings need new VPCast paper runs and explicit documentation.
Cancellation and replay protection reduce unnecessary requests; shorter waits
and immediate SCF entry may increase early participation; a longer absolute
age allows older data. No direction of aggregate load or PRR change is claimed
from these short tests. The paper should correct T1's units to milliseconds and
the velocity denominator to `2 Vmax`, and document the CPM lifetime, slot
duration/budget, speed references and application-submission timing under DCC.
No additional parameter approval is needed for the completed implementation.
