# Object freshness validation report

## Status

The implementation, full build, freshness tests, accounting regressions, link
PRR regressions and controlled leak checks passed. **Full Road 120 trajectory/RNG
invariance did not pass.** Do not describe this as a fully validated passive
observer or use the on/off timings as an isolated overhead estimate yet.

The continuation preserved all existing production changes. No scheduling, RNG
mapping, forwarding, object selection, wire timestamp, warm-up, plotting or
network-stack fix was made in response to the failed invariance check.

## Implementation retained

- New `CpmObjectFreshnessObserver.{cc,h,ned}` and `CpmObjectProvenance.h`.
- Measurement-only snapshot/cache provenance and a common valid-CPM receive hook
  in all six `CpService*.{cc,h}` implementations. Reception observation precedes
  algorithm-specific early returns and VP-CAST's legacy reference-time gate.
- Application CMake registration, the optional observer in INET `World.ned`, and
  observer enable/scalar-recording lines in Road 120 and InTAS configurations.
- Tests, runners, overhead probe and documentation in this directory.

The primary vector is `cpmPhysicalObjectFreshUpdateIntervalVehicle`, with
separate RSU and combined-local/CPM vectors. Eligibility is the existing
perception evaluation's `0 < object-to-receiver 2D distance <= 500 m`, excluding
the receiver itself. It is independent of the PRR transmitter-to-receiver range.
Metadata follows actual encoded entries and the 255-object cap. It preserves
physical lifetimes and original sensor-observation times across relays and RSU
cache repacking without changing ASN.1 bytes. Legacy interval metrics remain.

## Completed checks

| Check | Result |
| --- | --- |
| Full project build | PASS; `build-final.log` reaches all targets |
| Freshness controlled cases | 17/17 PASS |
| Existing VP-CAST cases | 17/17 PASS |
| Existing weighted p-persistence cases | 23/23 PASS |
| Accounting controlled fixture | PASS; 10 submissions, 15 windows, 7 zero-traffic windows |
| Link PRR controlled cases | 15/15 PASS, plus the observer-disabled RNG comparison run |
| All six Road 120 integrations | PASS through 302.1 s; original 300 s warm-up retained |
| RSUs in each Road integration | Seven registered by the freshness observer and active in existing PRR/accounting checks |
| Received-object metadata in all six integrations | No missing/unresolved entries or invalid information timestamps |
| Observation exposure/gap accounting | Completed intervals, initial waits and censored durations reconcile |
| LeakSanitizer | Stress, object/receiver lifecycle, and termination cases exit 0 |
| Final source/config audit | 21 checks PASS; `git diff --check` PASS |
| Full Road observer-on/off RNG/trajectory invariance | **FAIL / unresolved** |

Freshness cases cover cross-station ID collisions, multiple reporters, duplicate
relay copies, out-of-order data, local sensing, eligibility exit/re-entry, object
and receiver departure, external-ID reuse, missing provenance, valid object ID
zero, future timestamps, RSU repacking, all six encoders and their 255-entry cap,
warm-up, a 100-second gap, 10,000-message sustained traffic, separate receiver
roles, the common reception hook, coincident boundaries and termination.
The 10,000-message test rejects duplicate updates, retains only four pair states,
peaks at at most 1,001 metadata records, and prunes metadata during idle time.

The final source audit removes only explicitly listed measurement additions
before comparing each service to its pre-change snapshot. Every original
algorithm statement remains byte-for-byte intact. All six service NED files,
the existing shared transmission-accounting files, PRR observer files, and both
RSU XML files are byte-identical to that snapshot. Existing scenario settings,
including warm-up and algorithm parameters, are unchanged apart from enabling
the new observer and its scalar recording. The 109-RSU InTAS XML is preserved;
a new InTAS integration was not run for this task.

## Final Road 120 accounting and freshness results

These are short correctness runs, not paper performance rankings.

| Method | App submissions | Forwarded submissions | Actual air CPMs | Vehicle fresh intervals | RSU fresh intervals |
| --- | ---: | ---: | ---: | ---: | ---: |
| Proposed | 1,926 | 0 | 1,872 | 69,215 | 2,376 |
| Static | 2,879 | 0 | 2,820 | 71,080 | 2,719 |
| ETSI | 2,776 | 0 | 2,694 | 46,974 | 1,638 |
| LookAhead | 2,637 | 0 | 2,573 | 66,751 | 2,579 |
| VP-CAST | 43,005 | 40,066 | 4,802 | 18,212 | 600 |
| Weighted p-persistence | 25,981 | 22,998 | 4,701 | 21,299 | 880 |

The existing independent accounting probe serializes/decodes submissions and
checks sizes, encoded object counts, forwards, submission counts, throughput
windows, final bytes and durations. All six runs passed. Proposed additionally
passed 100 ms decision/history/all-object checks and suppressed 942 decisions:
832 redundant-object and 110 empty-object decisions, without submissions or
transmitted-byte samples.

The existing accounting validator assumes scalar files contain application
totals, whereas the scenario disables application scalars but enables observer
scalars. Validation therefore used a separate symlinked vector/trace-only view;
original result files and production recording settings were not changed.
Application scalar accounting was independently covered by its controlled
fixture. This is the pre-existing harness limitation, not an accounting change.

Corrected PRR conservation held for each role and each 25 m band:

| Method | Eligible pairs | Successful | Failed | Censored at termination |
| --- | ---: | ---: | ---: | ---: |
| Proposed | 34,552 | 33,468 | 1,061 | 23 |
| Static | 48,040 | 46,071 | 1,969 | 0 |
| ETSI | 47,215 | 45,177 | 2,017 | 21 |
| LookAhead | 42,334 | 40,836 | 1,498 | 0 |
| VP-CAST | 89,505 | 74,264 | 15,241 | 0 |
| Weighted p-persistence | 81,610 | 64,571 | 17,039 | 0 |

## Runtime, memory and failed invariance check

Three serial samples per mode used Proposed through 301.1 s, the same topology,
the same configured OMNeT++ seed set 0 and SUMO seed 23423, the verified working
library order and production networking. Both modes contain the observer module;
the disabled mode sets `*.cpmObjectFreshness.enabled=false`, so component IDs do
not change merely because the observer module is omitted. A test-only finish
probe reads the existing global RNG draw count without drawing a sample.

| Observer | Runtime samples (s) | Median runtime | Peak RSS samples (KiB) | Median peak RSS |
| --- | --- | ---: | --- | ---: |
| Enabled | 11.17, 10.08, 9.92 | 10.08 s | 380,536; 361,124; 361,132 | 352.7 MiB |
| Disabled | 9.56, 10.41, 10.29 | 10.29 s | 359,656; 384,548; 385,220 | 375.5 MiB |

**These are observed scenario costs, not a causal observer-overhead estimate.**
The trajectories/workloads differ. The lower enabled median does not show that
the observer improves performance. Its isolated runtime/memory overhead remains
unquantified. Peak retained state in the six longer integrations was at most
28,953 receiver/object pairs, 2,047 message-metadata records and 8,818 metadata
entries. There are no full-packet logs or unbounded per-object histories in
production; result vectors are streamed using OMNeT++ recording.

The original strict benchmark stopped at its first failed comparison; it was
not weakened to make the test pass. That first on/off pair produced respectively
691/681 application submissions, 658/639 air transmissions, and 3,876/3,646 RNG
draws. Further same-configuration disabled controls also differed (for example
3,729 and 3,935 draws), so existing run-to-run variability confounds the on/off
comparison. Later serial repetitions were internally repeatable within each
mode but still differed across modes (3,623 enabled versus 3,935 disabled draws).

A bounded, test-only RNG trace through the first exchanges found 1,063 identical
calls by value, range and calling component before the first on/off difference:
the enabled run had an additional MAC-contention draw at
`World.node[78].wlan[0].mac.hcf.edca.edcaf[1].contention` at 300.490644 s.
No traced RNG call originated in the freshness observer. Its source contains no
RNG calls, and the deterministic freshness fixtures assert unchanged draw counts.
This proves absence of direct RNG sampling in the observer, **not** full
integration trajectory invariance.

The first RNG-audit attempt inherited its preload library into the SUMO child,
triggering OMNeT++'s duplicate-library startup guard and a connection timeout.
The diagnostic was corrected to clear its own preload environment after loading;
the subsequent bounded traces completed. No launcher file was changed.

INET's radio set and the local environment model contain pointer-ordered
containers, making allocation/order sensitivity a plausible contributor. A
temporary diagnostic shim sorting radio deliveries by radio ID did not restore
on/off equivalence. It was never installed in production. **The exact root cause
of the trajectory divergence is not established.** Do not dismiss the failure
as harmless, claim RNG invariance, or change RNG mappings/network ordering on
this evidence. Further isolation is required before certifying passivity.

## Leak checks and remaining limitations

Standalone GCC LeakSanitizer passed the 10,000-message stress, lifecycle and
termination cases outside the sandbox with exit 0 and no new leak reports.
The initial sandbox attempt could not inspect threads (and one preload attempt
crashed); these were tooling failures, not established application defects.
Stale `lsan.6`/`lsan.8` files document those failed sandbox attempts. The verified
summary distinguishes them from the successful runs. Existing controlled tests
also reported no undisposed OMNeT++ objects and checked finish/departure cleanup.
This is not a full-duration Road/InTAS heap audit or an exhaustive address/UB
sanitizer run.

Other retained limitations:

- Raw-byte decoding into a new CPM wrapper lacks provenance; the observer counts
  unresolved metadata instead of guessing. All tested production paths retained
  typed wrappers and had complete metadata.
- The transmitted `measurementDeltaTime` sign remains unchanged pending separate
  approval. The new measurement uses the original unclamped sensor timestamp.
  Existing state serialization can read current kinematics for an older sensor
  track; this observer conservatively follows the last actual sensor observation.
- Eligibility follows the simulation's environment updates, not continuously
  interpolated crossings. Warm-up is unchanged. A later plotting subwindow needs
  an explicit boundary convention.
- Zero-duration episodes at coincident boundaries remain counted for conservation
  and are flagged by `zeroExposureEpisodes`; they contribute zero exposure.
  Completed-interval CDFs still require the zero-update/censoring companion data.
- Full-duration paper simulations, a new InTAS integration, full on/off checks
  for every method and a causally matched overhead benchmark remain outstanding.
  The observed Road RNG/trajectory failure prevents claiming those checks pass.
- Graph scripts continue reading the legacy metric and have not been modified.

## Evidence

Artifacts are under `/tmp/artery-object-freshness/`:

- `build-final.log`, `controlled-final.log`, `vpcast-final.log`, `weighted-final.log`.
- `integration-final/road120-*`: original final logs, vectors, scalars and test-only accounting traces.
- `integration-final-accounting.log`, `integration-final-prr.json`, `integration-final-validation.jsonl`.
- `prr-regression.log`, `source-audit-final.json`, `leak-summary-verified.json`.
- `benchmark.log`: original failed strict benchmark.
- `performance/summary.json`: all six serial timing/RSS samples and their differing workloads.
- `rng-audit/`: bounded RNG diagnostics and the unsuccessful, test-only stable-order experiment.

No correction to unrelated algorithm/network behavior has been applied. The
remaining acceptance issue is full-run passivity/RNG reproducibility, not the
correctness of the accounted interval, packet-size or PRR totals above.
