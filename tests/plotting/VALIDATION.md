Validation completed on 2026-10-19 for the two essential changes only. Earlier work in the workspace was preserved.

The six CPM services now initialize all snapshot fields, use an explicit `mVdpSnapshotValid` flag, and lazily capture the snapshot before cold reception/perception. Existing generation/response refresh points remain. Zero latitude/longitude no longer cause repeated refreshes. Static's RSU response also has an explicit first-use guard. A completed capture may contain ASN.1 unavailable values when facilities are unavailable; cache validity indicates initialization, not geographic availability.

Production changes are limited to snapshot defaults, validity guards, and the completion flag. No decision rules, forwarding rules, timers, thresholds, observer definitions, wire format, layouts, warm-up, or RNG configuration changed. A source-hash comparison against the task-start snapshot found changes only in the twelve CPM service files listed below. Early reception histories/results can change because they now use initialized positions; this validation does not claim full-run reproducibility.

Validation results:

- Full `cmake --build build -j 4`: passed. Existing Boost deprecation notices only.
- Cold-start fixture: passed all six services as vehicles and RSUs (12 combinations). It checks every initial field, real reception before any generation, valid zero latitude/longitude, station identity/type, cached reception after the underlying position changes, explicit refresh, unchanged RNG draw count within this fixture, and clean module destruction. Test instances skip production initialization/generation and disable forwarding so the receive/cache behavior is isolated; real integrations below exercise normal initialization.
- Two new headless Road 120 runs completed at 302.1 s, with the unchanged 300 s warm-up. Both used envmod-first library loading in staged `/tmp` copies; launchers were not edited. All seven RSUs were registered and submitted CPMs. Proposed suppression/history handling, encoded submission accounting, freshness conservation/metadata, and corrected link-layer PRR validations passed.
- Proposed: 2,009 application submissions, 263,958 encoded bytes, 7,782 encoded object entries; 782 suppressed decisions (690 redundant, 92 empty); 100 ms decisions and cleared histories verified. There were 103 zero-byte active windows. PRR: 32,761 received / 33,999 resolved, with 11 termination-censored pairs excluded; 1,924 actual radio transmissions.
- Static: 2,969 application submissions, 370,740 encoded bytes, 10,751 encoded object entries; 24 zero-byte active windows. PRR: 49,033 / 51,108 resolved; 2,897 actual radio transmissions.
- Existing production accounting fixture rerun: passed, including idle stations, late entry, departure, final partial windows, 255 encoded entries from 260 selected, and a positive-size CPM with zero objects. Ten submissions, 6,164 encoded bytes, seven zero windows, 15 windows total. The plotter also processed its real `.vec`/`.sca` outputs and reported unavailable PRR/freshness instead of substituting legacy data.
- Plotting fixtures: 16 tests passed. Coverage includes PRR censoring and empty bins; cumulative-vector event counting; actual encoded byte/object totals; idle/partial windows; active-station versus network weighting; initial network silence; exact rebinning when plot boundaries cut windows; all-idle streams without packet vectors; missing/nonzero cut-window inputs; separate vehicle/RSU freshness; missing corrected data; invalid samples/counts; native/CSV-R agreement; large spilled CSV fields; multiple runs; and figure/report generation for all six methods.
- Six-method plot totals reconciled with independent submission traces, including 34,273 forwarded VP-CAST and 21,433 forwarded weighted p-persistence submissions in the reused short results. All encoded bytes/object counts and fixed-window bytes matched; freshness interval sample counts matched scalar completed intervals; PRR opportunity counts conserved. Four baseline inputs were reused from earlier validation; they were **not** rerun after this snapshot fix. These mixed short-run figures demonstrate input correctness, not final comparative research results.
- Sixteen sample figures were generated and PRR, vehicle CDF, and station-throughput figures visually inspected. No guide lines or error bars were added; window steps have no artificial drop to zero at their endpoints. Missing metrics are reported in the console and JSON. Samples and logs are under `/tmp/artery-final-essential/` (`plots-six`, `plots-accounting`, `integration`, `snapshot-tests`).
- Road 120 and InTAS still expose Proposed, Static, ETSI, LookAhead, VPCast, and wPPersistence. All six service types also loaded in the cold-start fixture.
- `git diff --check` and Python syntax checks passed. No full-duration Road 120/InTAS experiment or further RNG investigation was run.

The plotter uses received/resolved `cpmLinkPrr.all.*` scalar counts (25 m transmitter–receiver bins, pooled vehicle/RSU pairs); corrected vehicle freshness vectors with separate RSU figures and coverage tables; encoded packet sizes; submission event counts; encoded object counts; and joined byte/duration windows. Per-active-station rates divide by station-active seconds; total network rates divide by run-wall seconds, including initial silence before the first station becomes active. Scalars always cover their whole recorded period; vector filters do not time-slice scalars. Native text vectors and CSV-R are streamed; window joins use temporary disk storage and a bounded cache. Full definitions and input options are in `scripts/README.md`.

Remaining limits: CDFs/boxplots above the sample cap use uniform reservoir samples. Scalar PRR/coverage cannot be cropped in time. Run-end metadata is absent from these results: if every station departs early, network exposure after the final recorded station window cannot be inferred; Road 120's persistent RSUs supply windows to termination. Missing metrics stay unavailable. Native SQLite results/CSV-S are unsupported. RX throughput and RX object-count plots remain explicitly labeled legacy descriptions; those measurement implementations were not changed. No full experiment, long-run leak analysis, performance benchmark, or strict reproducibility claim is part of these two tasks.

Exact files changed/added in this task (other pre-existing changes remain):

- `src/artery/application/CpService.cc`
- `src/artery/application/CpService.h`
- `src/artery/application/CpServiceStatic.cc`
- `src/artery/application/CpServiceStatic.h`
- `src/artery/application/CpServiceEtsi.cc`
- `src/artery/application/CpServiceEtsi.h`
- `src/artery/application/CpServiceLookAhead.cc`
- `src/artery/application/CpServiceLookAhead.h`
- `src/artery/application/CpServiceVPCast.cc`
- `src/artery/application/CpServiceVPCast.h`
- `src/artery/application/CpServiceWeightedPPersistence.cc`
- `src/artery/application/CpServiceWeightedPPersistence.h`
- `tests/object_freshness/Fixture.cc`
- `tests/object_freshness/Fixture.ned`
- `tests/object_freshness/fixture.ini`
- `tests/object_freshness/run.py`
- `scripts/masterplot.py`
- `scripts/requirements-plotting.txt`
- `scripts/README.md`
- `tests/plotting/test_masterplot.py`
- `tests/plotting/VALIDATION.md`

The original `/home/dreamwalker/omnetpp-5.6.2/masterplot.py` was left unchanged. The repository copy retains that directory as its default input location; `--input-dir` or repeated `--file METHOD=PATH` works anywhere.

Commands from the repository root:

```sh
cmake --build build -j 4
python3 tests/object_freshness/run.py --case snapshots --output /tmp/artery-snapshot-check
python3 -m venv /tmp/artery-plots
/tmp/artery-plots/bin/pip install -r scripts/requirements-plotting.txt
/tmp/artery-plots/bin/python tests/plotting/test_masterplot.py
/tmp/artery-plots/bin/python scripts/masterplot.py --input-dir /path/to/results --output-dir /path/to/figures --start 300 --end 360
git diff --check
```

Set the final plot end to the actual recorded measurement end. Match scalar companions by stem or specify `--scalar-file METHOD=PATH`. Do not reuse the short validation figures as paper results. The validation environment installed plotting dependencies only under `/tmp/artery-final-essential/plot-deps`; it did not change system Python.
