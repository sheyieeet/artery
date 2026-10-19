# Physical-object freshness measurements

Validation status: functional/accounting/PRR and controlled leak checks pass,
but full Road 120 observer-on/off RNG invariance remains unresolved. See
[VALIDATION.md](VALIDATION.md) before using these results in the paper.

The shared `artery.application.CpmObjectFreshnessObserver` is enabled in Road
120 and InTAS. It does not replace the legacy `cpmObjectUpdateInterval`, change
any forwarding or selection rule, or alter CPM ASN.1 bytes. Plot scripts still
select the legacy measurement and must be updated separately before corrected
results are used in the paper.

All six services call the common measurement entry point immediately after
validating a received CPM, before any algorithm-specific processing/early
return. The observer does not depend on legacy `CpmReceived` emissions: four
services emit that legacy signal only on their RSU branch. Those emissions and
all legacy metrics remain unchanged.

## Population and eligibility

The primary population is CPM-equipped vehicle receivers; RSU receivers have
separate signals and scalar groups. Every ground-truth dynamic object with
`0 < 2D distance(object centre, receiver position) <= 500 m` is eligible,
excluding the receiver's own SUMO identity. This follows the existing
perception-rate denominator and its twenty 25 m bands. It is not a radio-link
condition: neither the original reporter nor an immediate relay must be within
500 m. The PRR observer independently uses radio-transmitter/receiver positions
at transmission start. Direct sensor ranges (150 m vehicle, 200 m RSU radar)
remain unchanged and do not limit cooperative awareness.

Eligibility follows the positions available at each TraCI/environment refresh
and object insertion/removal. There is no invented continuous interpolation or
line-of-sight filter. A spatial grid limits range-query work. All eligible
objects are registered, including objects never received in any CPM.

## Definition and clocks

Each receiver/physical-object stream has a greatest information timestamp.
Only strictly newer information advances it. The primary CPM-only stream and
the optional combined local-sensing/CPM stream maintain independent watermarks.
Different origins describing the same physical object share one stream;
different physical objects never share it, even when station/object IDs collide.

The first accepted update records an initial waiting time, not an inter-update
interval. Later accepted updates record elapsed receiver time since the previous
accepted update. Equal-time deliveries coalesce; stale, repeated and out-of-order
information never reset the arrival clock. A short inter-update interval is not
itself an Age of Information or latency measurement.

Entering the eligibility range starts an episode. Range exit, target departure,
receiver departure and simulation termination close it and censor its unfinished
gap. Compact knowledge watermarks survive range exits while both physical
lifetimes remain active; an old CPM cannot become fresh on re-entry. Loss of a
local sensor track alone does not end eligibility. Source-station departure does
not invalidate an otherwise live relayed CPM's provenance.

Warm-up is the existing OMNeT++ warm-up period, unchanged. Pre-warm-up knowledge
is retained, but interval/initial-wait clocks begin a new episode at warm-up.
No completed interval bridges that boundary. Using a later plotting window
requires its own explicit boundary convention; the observer does not know
`masterplot.py` command-line options.

## Metadata and timestamp choice

Snapshots carry a weak reference to the exact environment object and the
unclamped last actual sensor-observation time from LEM tracking. At message
assembly, metadata mirrors the encoder's ordered selection and 255-entry cap.
It is indexed by actual encoded entry order, never by CPM object ID. The weak
object owner identifies a physical lifetime, including reuse of a SUMO name.

The metadata registry is keyed by a weak shared CPM-wrapper owner. Artery's
typed application buffers share that owner across GeoNet packet copies and the
two forwarding methods. Cached RSU reports also carry their provenance when
repacked: creating a new CPM reference time does not renew the object's time.
No ASN.1 field, packet length, digest or additional simulated RNG draw is added.

The existing wire `measurementDeltaTime` is `referenceTime - lastDetected`,
whereas the bundled Release 2 ASN.1 says measurements before the reference have
negative deltas. Its clamping also loses time information. This observer uses
the original simulation timestamp directly and does not repair the transmitted
field without separate approval. Existing snapshots read current ground-truth
kinematics even when sensor tracking is older; this metric deliberately follows
the last sensor observation and does not pretend the older track was measured
again at message assembly. `objectAge` is not an information timestamp.

Raw byte decoding into a new CPM wrapper has no provenance owner. Such entries
are counted as missing metadata, never guessed from position or object ID.
Current Road 120/Artery typed paths must have zero missing/unresolved metadata;
the integration validator enforces this. Supporting an external/raw-only packet
source would require a separate simulation-side propagation mechanism.

## Outputs and interpretation

Vectors on `World.cpmObjectFreshness` are:

- `cpmPhysicalObjectFreshUpdateIntervalVehicle`: corrected primary CDF input.
- `cpmPhysicalObjectFreshUpdateIntervalRsu`: separate RSU receiver input.
- `combinedPhysicalObjectFreshUpdateIntervalVehicle` and `...Rsu`: companion
  combined sensing/CPM intervals.
- For each prefix/population, `InitialWait`, `CensoredInitialWait` and
  `CensoredGap` vectors describe discovery delays and unfinished observations.

Scalars `objectFreshness.{cpmOnly,combined}.{vehicle,rsu}.*` include eligible
episodes and seconds, zero/one-update episodes, completed intervals, fresh
updates, initial waits, censored durations and ending reasons. Completed-interval
CDFs are conditional on receiving two updates; they must be interpreted together
with zero-update counts and censored gaps. Silent objects do not produce fake
zero intervals or artificial failure durations.

For normal positive-duration episodes, accounting reconciles:

```
freshUpdates = completedInitialWaits + completedIntervals
eligibleEpisodes = completedInitialWaits + censoredInitialWaits
zeroUpdateEpisodes = censoredInitialWaits
eligibleSeconds = completedInitialWaitSeconds
                + completedIntervalSeconds + censoredSeconds
```

Instantaneous episodes at coincident boundaries remain counted for conservation
and are separately identified by `zeroExposureEpisodes`; they add no exposure.

Diagnostic scalars report metadata misses, invalid future times, departed
objects and peak retained state. Unknown physical provenance is skipped without
substituting object zero. CPM object ID zero remains valid.

## Memory and validation

There is no per-object event history. There are two watermarks/gap states per
observed active receiver/object pair, including previously eligible pairs outside
range until an actual lifetime ends. The bound is active receivers times active
objects, not elapsed run length. Message metadata holds no full packet or strong
CPM/object reference and is pruned every second, including idle periods. Its
size follows live queued/cached messages plus at most one pruning period.
Vector samples stream through OMNeT++ result recording.

After building with `cmake --build build --parallel 2`:

```
python3 tests/object_freshness/run.py
python3 tests/object_freshness/integration.py --method Proposed
python3 tests/object_freshness/validate.py /tmp/artery-object-freshness/integration/road120-Proposed
```

The integration runner uses the verified envmod-first loading order without
editing launchers. SUMO requires permission for its local TraCI socket.
`--disabled` keeps the observer module in the topology but disables its hooks,
allowing overhead/RNG comparisons without changing component IDs between runs.
`--accounting-probe PATH` enables the existing independent accounting validator
for tests only; production measurements do not add packet logs.
