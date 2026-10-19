# Weighted p-persistence corrections: validation record

Validated on 2026-10-19. Run artifacts, snapshots and the production-only diff
are under `/tmp/artery-weighted-fixes/`.

## Modified files and scope

- `src/artery/application/CpServiceWeightedPPersistence.cc`: typed forwarding
  timers, full message identity, marking originated messages, last-hop geometry,
  strict probability comparison, pending/seen-state separation, original-time
  expiration, clipped GN lifetime and lifecycle cleanup.
- `src/artery/application/CpServiceWeightedPPersistence.h`: message key,
  phase/state definitions, compact history/expiry index and forwarding helpers.
- `src/artery/application/CpServiceWeightedPPersistence.ned`: the new approved
  `maxCpmAge = 2s` parameter. Initial `waitTime = 5ms`, additional `tau = 5ms`
  and `transmissionRange = 500m` retain their existing defaults.
- New `tests/weighted_pp/{Fixture.cc,Fixture.ned,fixture.ini,run.py,README.md}`
  and this validation record.

SHA-256 comparison against pre-change snapshots found exactly those three
modified production files and 49 unchanged protected files. Proposed, VP-CAST,
Static, ETSI, LookAhead, shared `CpmService`, the link-layer PRR observer,
existing validation suites, both scenario INIs and RSU layouts are unchanged.
The seven Road 120 and 109 InTAS RSUs remain configured as before. No ASN.1,
object-selection, receive-metric, warm-up, launcher or graph changes were made.
InTAS was not simulated in this task.

## Build

`cmake --build build --parallel 2` completed successfully through all project
targets. Existing Boost deprecation notices remain. `git diff --check` passed.

## Controlled forwarding tests

All 23 cases passed using the real production forwarding implementation and
common accounting with a fixture-only lower-layer submission sink:

| Cases | Checks |
| --- | --- |
| probability, zero, equalProbability | Strict probability boundary, including a forced exact-zero draw at p=0; p=0 still permits the later probability-one fallback |
| minimum, geometry | Previous-forwarder distance, fresh VDP instead of stale serialization snapshot, nearest-copy minimum without initial-window cancellation |
| dateline, equator, staticPosition | Longitude wrap, valid zero coordinates, current static coordinates without a VDP |
| identity | New times at unchanged latitude, distinct payloads at one origin/time, different origins, full timestamp decoding, valid out-of-order CPMs |
| cancel, own | Fallback duplicate cancellation, payload release, replay protection before/after expiry, originated-message protection |
| expiryInitial, expiryFallback, expiryExact | Deadline during either wait and exactly at a timer boundary; no expired submission and no draw after initial expiry |
| late, short, expired, future | Original age preserved, residual GN lifetime rounded down, no request with less than 50 ms expressible validity, rejection of expired/future messages from forwarding |
| cleanup, departure | Idempotent cleanup; both pending phases with normal finish/deletion and direct destruction; no cached payload or timer survives removal |
| stress, fallbackStress, randomStress | Sustained arrivals, one draw and at most one submission per logical CPM, idle pruning, bounded pending and replay history |

Every probability sample was checked against the original global RNG 0 mapping.
The test hook consumes exactly one original random sample before substituting
specified boundary values; the random stress case uses the original values.
Global RNG draw totals matched expectations, including no draws for duplicates,
fallback timers, originated replays and messages expired before a decision.
The hook is test-only and was not loaded by integration simulations.

Each stress case injected 10,000 unique CPMs at 1,000/s, with initial duplicates:

| Case | Probability draws | Submissions at initial decision | Fallback submissions | Peak seen keys | Peak pending payloads |
| --- | ---: | ---: | ---: | ---: | ---: |
| stress, p=1 | 10,000 | 10,000 | 0 | 2,000 | 6 |
| fallbackStress, p=0 | 10,000 | 0 | 10,000 | 2,000 | 11 |
| randomStress, p approximately 0.5 | 10,000 | 5,010 | 4,990 | 2,000 | 11 |

Both maps and the expiry index were empty after quiescence without explicit
test-driven pruning. Weak pointers verified release of application-cached CPMs
after decisions, cancellation, expiry and destruction. OMNeT++ reported no
undisposed fixture objects. Retention is bounded by recent arrival rate and
validity, not an absolute byte cap or a process-RSS guarantee.

All 17 existing VP-CAST controlled tests also passed without modifying that
suite or implementation.

## Road 120 integration and accounting/PRR regression

Real headless simulations used the existing verified envmod-first library
loading procedure. SUMO required the local TraCI socket. Both runs reached
302.1 s and finished without simulation errors; warm-up remained 300 s.
All seven RSUs had active CPM services in each run. Existing PRR checks verified
vehicle/RSU role participation and distance-bin/role conservation.

| Method | Application submissions | Forwarded submissions | Actual air transmissions | Application payload bytes |
| --- | ---: | ---: | ---: | ---: |
| Corrected weighted p-persistence | 24,247 | 21,350 | 4,541 | 3,060,970 |
| Proposed regression | 1,907 | 0 | 1,860 | 247,841 |

The existing accounting probe independently serialized and decoded submitted
wire payloads. Recorded sizes, encoded object counts, transmission counts and
window bytes/durations matched actual submissions, including forwards.
Weighted p-persistence produced 563 windows, including six zero-traffic windows,
across 188 station services. All bytes and active durations were conserved,
including final-window flushing. Proposed produced 573 windows with 128 zeros
across 192 services.

Proposed retained 100 ms decisions, consumed received history and all-object
inclusion when sending. It suppressed 929 decisions: 830 redundant-object and
99 empty-object cases. Suppressed decisions produced no transmission or byte
samples. These are invariant checks, not a claim that separate stochastic runs
must produce identical aggregate totals.

The unchanged link PRR observer recorded these weighted totals:

```
eligible transmission-receiver pairs: 77,305
successful receptions:               60,613
resolved failures:                   16,646
censored at simulation termination:      46
```

Every pair was accounted for once; role and 25 m distance totals reconciled.
The denominator used actual air transmissions rather than application requests.
The submission/air gap remains possible under unchanged DCC and MAC behavior;
it does not imply repeat application forwarding of the same logical CPM.

The scenarios disable application scalars but enable PRR scalars. The existing
accounting validator assumes any scalar file supplies all application totals,
so accounting validation used a separate symlinked vector/trace-only view.
Original PRR scalars were checked independently by the PRR validator. No
production recording settings or measurement code changed for this harness
limitation. Controlled fixtures separately verified application totals.

## Remaining limitations and Section V.B

The approved 5 ms additional fallback approximates the reference method's
`WAIT_TIME + delta`; delta is not separately modeled. Application timers run
before DCC or MAC necessarily transmits neighboring copies. Cancellation cannot
recall previously submitted requests. New forwarded requests receive no more
than the remaining original CPM validity, rounded down in GN's 50 ms steps and
capped at the existing one-second network lifetime; already accepted MAC copies
can still transmit later. The age policy governs forwarding, while existing
receive/object metrics and successful link receptions retain their handling.

No arbitrary hop limit, retry cap, DCC-aware timer or queue-recall mechanism was
added. Exact copies of the same origin/time/body remain logical duplicates;
their physical rebroadcasts still form distinct PRR opportunities. Forwarding
holds constant CPM wrappers only while deciding; lower-layer queues continue
to own payloads they need for actual pending transmissions.

Section V.B should explicitly document:

1. Origin/full reference time/payload SHA-256 identity and marking local origins.
2. Immediate-forwarder/fresh receiver geometry and minimum distance during the
   initial window; `p = clamp(D / 500m, 0, 1)` is a normalization, not a geofence.
3. One probability draw, strict comparison, a 5 ms initial wait and an additional
   5 ms approximation, and a single probability-one fallback canceled by a
   duplicate received during fallback.
4. The CPM-specific 2 s absolute reference-time validity, replay keys retained
   until that deadline and payloads released after decisions; no age renewal.
5. Clipped network lifetime, its one-second ceiling/50 ms granularity, and the
   absence of an end-to-end hop budget or periodic retransmission mechanism.
6. Unchanged DCC timing/queue limitations, generation and vehicle/RSU roles,
   and the distinction between app submissions and actual-air PRR opportunities.

The article's TTL=100 experimental setting was not adopted as a new mandatory
parameter. The paper's weighted results need rerunning: corrected identity can
increase legitimate forwarding, while corrected distance and replay/expiry
handling can reduce other requests. These short tests establish behavior and
accounting correctness, not a performance ranking or optimal timer profile.
