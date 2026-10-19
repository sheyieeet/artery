# CPM link PRR observer and validation

`artery.inet.CpmLinkPrrObserver` measures the approved link-layer endpoint,
independently of the six CPM services. Road 120 and InTAS enable the same observer
in General. Existing algorithms, application transmission accounting, warm-up,
legacy observations and plots are unchanged.

At INET `transmissionStarted`, the observer recognizes the CPM application wrapper
inside the transmitted GeoNetPacket and keys a flight by `ITransmission::getId()`.
It snapshots active CPM-equipped host lifetimes, channel (carrier frequency and
bandwidth), and 2D antenna positions within 500 m. Transmitting/half-duplex receivers
remain eligible; loss mechanisms must not remove denominator entries. All current
CPM paths retain this typed wrapper, including forwarded CPMs without a POC.

The radio `packetSentToUpper` signal fires inside `Radio::endReception`, while
`getReceptionInProgress()` still exposes the exact physical ID. The observer
temporarily associates that ID with the receiving NIC and MAC-frame tree ID.
Only a matching **MAC** `packetSentToUpper` marks a success. Radio handoffs can
contain erroneous frames. A tree ID is only a short-lived join across the handoff,
never a physical-transmission key. Current unfragmented broadcasts pass the same
frame through immediately; ambiguous associations and CPM fragmentation fail
explicitly instead of silently producing invalid results. There are no networking
stack, packet metadata or ASN.1 changes.

Completion uses arrival end times already cached by RadioMedium before its
transmissionStarted callback. A single observer timer, after zero-delay MAC events
at that time, resolves unmatched pairs as failures and frees their state. Removing
a station censors its unfinished receive opportunities and unfinished receptions
of its transmissions. Already accepted receptions and already completed losses
remain resolved. At simulation finish, unresolved pairs are censored. A new
station never joins an older transmission's receiver snapshot.

Only starts at/after the existing warm-up period are measured. Memory holds active
stations, in-flight pair metadata and aggregate counters, never full packets,
historical packet sets or per-packet output vectors. The observer performs no
encoding, decoding, error-model evaluation or random draws.

## Results

Corrected results are aggregate OMNeT++ scalars under `World.cpmLinkPrr`:

- `cpmLinkPrr.airTransmissions`: actual CPM air transmissions, including relays.
- `cpmLinkPrr.{all,VV,VR,RV,RR}.total.{eligible,received,failed,censoredDeparture,censoredTermination,resolved,ratio}`.
- Replace `total` with `distance0to25m`, `distance25to50m`, ..., `distance475to500m`
  for 25 m intervals. Intervals include their lower edge; only the final interval
  includes its upper edge, 500 m. Zero distance between different stations is valid.
- `ratio = received / resolved`, where `resolved = received + failed`.
  `eligible = resolved + censoredDeparture + censoredTermination`.
  No resolved opportunities gives NaN, not a misleading zero. Validation JSON uses null.
- Peak active-transmission, opportunity and frame-association counts expose memory
  occupancy without communication logs. Duplicate MAC callbacks are counted
  diagnostically and cannot increase successful pairs twice.

VV/VR/RV/RR mean vehicle-to-vehicle, vehicle-to-RSU, RSU-to-vehicle and RSU-to-RSU.
Pool counts before dividing; do not average distance-bin or station ratios.

Legacy `cpmExpectedDistance`, `cpmReceivedDistance` and weighted p-persistence's
`cpmPrr` remain unchanged for compatibility. They are application observation
ratios, **not corrected link PRR**. Existing graph scripts still read their old
inputs; changing graphs is a separate task.

## Reproducing tests

From the repository root, after `cmake --build build --parallel 2`:

```sh
python3 tests/cpm_link_prr/run.py --build-only
python3 tests/cpm_link_prr/run.py --case lossfree
python3 tests/cpm_link_prr/run.py --case loss
python3 tests/cpm_link_prr/run.py --case corrupt
python3 tests/cpm_link_prr/run.py --case repeated
python3 tests/cpm_link_prr/run.py --case duplicate
python3 tests/cpm_link_prr/run.py --case departure
python3 tests/cpm_link_prr/run.py --case senderDeparture
python3 tests/cpm_link_prr/run.py --case termination
python3 tests/cpm_link_prr/run.py --case entry
python3 tests/cpm_link_prr/run.py --case eligibility
python3 tests/cpm_link_prr/run.py --case range
python3 tests/cpm_link_prr/run.py --case warmup
python3 tests/cpm_link_prr/run.py --case rng
python3 tests/cpm_link_prr/run.py --case rng --disable-observer
python3 tests/cpm_link_prr/run.py --case stress
python3 tests/cpm_link_prr/run.py --case noReceivers
python3 tests/cpm_link_prr/run.py --method Static
python3 tests/cpm_link_prr/run.py --method VPCast --dcc-probe
```

Methods also accept Proposed, ETSI, LookAhead and wPPersistence. SUMO runs need
permission for local TraCI sockets in a restricted sandbox. Output defaults to
`/tmp/artery-prr-validation`; `validate.py <run-directory> ...` checks scalars and
streams existing submission vectors. The verified envmod-first library loading
procedure is confined to the test runner; existing launchers are untouched.

Fixtures use real INET MACs/radios with controlled sources and CPM-equipment
markers. They send identical CPMs without a POC; do not use their bypassed
middleware as a transmission-algorithm test. The corrupt case injects a bit error
at the radio handoff; the duplicate case repeats an observation callback only.
The stress case checks 1,000 transmissions without growing retained history.
The RNG case compares draw counts, transmission times and accepted-frame counts
with and without the observer. A separate existing accounting regression checks
the real Proposed suppression rules, 100 ms schedule, history clearing, payload
bytes, zero traffic windows and submission counts.

`--dcc-probe` loads test-only instrumentation which counts existing DCC release
and drop hooks and samples queues at service finish. It stores only per-DCC
counters. Drop events cover all packet types: the existing overflow hook's
incorrect packet pointer prevents reliable CPM-specific drop attribution.
These hooks do not modify scheduling, queue policy or protocol contents and are
not installed by the production PRR observer.
