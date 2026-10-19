# CPM application transmission accounting

All six CPM implementations inherit `CpmService`. It wraps their existing
service-to-middleware `request()` calls and records one `CpmSubmitted` event only
after that call returns. This includes ordinary generation, selected-link
responses, and rebroadcasts. A middleware request counts even if the network
subsequently drops or duplicates it. It is **not** confirmation of a MAC/PHY
transmission, reception, or delivery. The existing `CpmSent` signal and legacy PRR
counters retain their existing behavior.

`cpmPacketSize` measures the actual serialized Application layer in bytes. BTP,
GeoNetworking, security and MAC/PHY overhead are excluded. `CpmTxObjectCount`
counts the perceived-object entries in the submitted ASN.1 containers, including
forwarded payloads. It therefore reflects the existing 255-object cap. A valid
CPM without a Perceived Object Container has a positive payload size and zero
objects. A suppressed CPM produces no submission, size or object-count sample.

One measurement timer per service closes globally aligned one-second windows.
It draws no random numbers and does not invoke or reschedule any algorithm.
Boundary-time algorithm events have their original priority; the recording
message runs afterward so those submissions belong to the window ending there.
Every active interval produces a sample, including zero traffic. Measurement
begins at `max(service initialization, warmup period)` and ends at service
`finish()` (called by TraCI before vehicle deletion, and at normal simulation
termination). Entry, warm-up, departure and final windows can be shorter than
one second; the final window is flushed even if no further CPM is generated.
Zero-duration lifetimes have no defined throughput but retain submission totals.

Canonical OMNeT++ output is identical for all six methods:

- `transmission:vector` counts `CpmSubmitted`, not decisions or received packets.
- `cpmPacketSize:vector` and `cpmTxObjectCount:vector` record each submission.
- `cpmTxWindowBytes:vector` records the bytes in each window.
- `cpmTxWindowDuration:vector` records its active duration in seconds.
- `cpmTxThroughput:vector` is `8 * window bytes / active duration` in bits/s.
- `cpmTxThroughput:mean` and `cpmTxAverageThroughput` are duration-weighted lifetime
  means, `8 * total measured bytes / total measured active seconds`.
- `cpmApplicationSubmissions`, `cpmTxPayloadBytes`, and `cpmTxActiveDuration` retain
  measured totals. Scalar output honors the scenario's scalar recording setting.

For comparison across stations, `8 * sum(bytes) / sum(active durations)` gives
mean application traffic per active station. To obtain aggregate network traffic
in a global one-second bin, sum its station byte contributions and divide by the
bin's wall-clock duration, including partial station contributions. These answer
different questions. Do not average partial-window rates with equal weight or
count inactive time after a station has departed. Existing CSV logging hooks
receive the corrected TX object count, window rate, window bytes and duration;
their existing layout is preserved. Plotting/aggregation scripts are unchanged.

## Running validation

From the repository root, using an existing CMake build:

```sh
cmake --build build --parallel 2
python3 tests/cpm_accounting/run.py --build-only
python3 tests/cpm_accounting/run.py --fixture
python3 tests/cpm_accounting/run.py --method Proposed
# Repeat --method for Static, ETSI, LookAhead, VPCast, wPPersistence.
python3 tests/cpm_accounting/validate.py /tmp/artery-cpm-accounting/fixture /tmp/artery-cpm-accounting/road120-*
```

The Road 120 tests use a 300–302.1 s simulation interval and need permission to
open the local SUMO TraCI socket. The runner uses the verified envmod-first
library loading order; it does not edit launchers. Each run stages configurations
and outputs in its own `/tmp` directory. Override the directory using `--output`.

The test library passively observes real submissions and independently
serializes and decodes their Application payloads. It asserts matching vector
sizes, object counts, submission counts, window alignment, zero traffic,
byte/time conservation, final-window flushing, Proposed suppression, 100 ms
decisions and consumed received history. Natural rebroadcasts from both relay
methods must appear in the measured submissions. All seven Road 120 RSUs must
have active CPM services.

A separate deterministic headless fixture uses the real accounting base and a
test-only lower-layer sink, without SUMO. It covers warm-up exclusion, exact
boundary submissions, an entirely idle station, late entry, mid-window departure,
normal simulation termination, a CPM without perceived objects, and selection of
260 objects with the existing encoder cap of 255.

The bundled ASN.1 decoder fails to round-trip the synthetic 255-object stress
payload (also observed for a synthetic 254-object payload); this is outside the
accounting changes. The stress test checks its validated encoder input and actual
serialized size instead. Every real Road 120 payload must independently decode.
The test observer and fixture hooks are loaded only by this test runner and are
not linked into the production library.
