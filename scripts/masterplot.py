#!/usr/bin/env python3
"""Stream corrected CPM results for all six methods (numpy and matplotlib).

Examples:
  python3 scripts/masterplot.py --input-dir /path/to/results
  python3 scripts/masterplot.py --file Proposed=/path/Proposed-0.vec --end 302.1
  python3 scripts/masterplot.py --file Proposed=/path/Proposed.csv \
      --scalar-file Proposed=/path/Proposed.sca

The default input directory preserves the original external script's location,
/home/dreamwalker/omnetpp-5.6.2. Explicit --file paths remain relative to the cwd.
Matching .sca siblings supply corrected PRR and freshness coverage. No legacy
PRR/update-interval fallback is used. Scalars cover their WHOLE recorded run;
--start/--end filter vectors only. Use runs recorded for the desired scalar
measurement period, rather than treating scalar counts as time-filterable.

TX rates use paired one-second byte/duration windows, including idle and partial
windows. Mean active-station rate = 8*bytes / summed station-active seconds.
Network rate = 8*bytes / observed run-wall seconds (averaged across pooled runs).
If plot boundaries cut a recording window, encoded packet events rebin its bytes
exactly; no assumed uniform byte distribution is used. Window exposure remains
exact. Reception throughput is an explicitly labeled legacy descriptive plot.

Native .vec and CSV-R vector inputs are streamed. Window joins use a temporary
SQLite file with a bounded cache, since vector blocks need not be interleaved.
No packets or per-packet histories are stored. CSV fields spill to disk after
64 KiB. Reservoir-sampled CDFs/boxplots are exact up to --sample-cap, uniformly
sampled above it. All means/counts/totals use every observation. No confidence
intervals are inferred from pooled observations. See scripts/README.md.
"""

import argparse
import csv
import json
import sqlite3
import math
from pathlib import Path
import random
import re
import shlex
import sys
import tempfile
from itertools import zip_longest

import numpy as np
import matplotlib
matplotlib.use("Agg")  # Save figures without GUI buffers.
import matplotlib.pyplot as plt

BASELINES = {
    "Proposed": "Proposed.vec", "Static": "Static.vec", "ETSI": "ETSI.vec",
    "LookAhead": "LookAhead.vec", "VPCast": "VPCast.vec",
    "wPPersistence": "wPPersistence.vec",
}
MARKERS = ['o', 's', '^', 'D', 'v', 'X']
COLORS = ['#000000', '#d55e00', '#0072b2', '#e69f00', '#009e73', '#0045e6']
LINESTYLES = ['-', '--', '-.', ':', (0, (3, 1, 1, 1)), (0, (5, 1))]
SERIES = {
    'cpmChannelLoad': ('Channel busy ratio', 'Channel load', 'channel_load_time.png'),
    'cpmTxObjectCount': ('Encoded perceived objects per submitted CPM', 'Objects per CPM', 'tx_objects_time.png'),
    'cpmRxThroughput': ('Legacy received-throughput observations', 'Recorded rate (bit/s)', 'legacy_rx_throughput_time.png'),
}
PERCEPTION = {f'cpmPerceptionRate{d}': d for d in range(25, 525, 25)}
FRESH = {'vehicle': 'cpmPhysicalObjectFreshUpdateIntervalVehicle',
         'rsu': 'cpmPhysicalObjectFreshUpdateIntervalRsu'}
WINDOWS = {'cpmTxWindowBytes': 'bytes', 'cpmTxWindowDuration': 'duration',
           'cpmTxThroughput': 'rate'}
TARGETS = set(SERIES) | set(PERCEPTION) | set(FRESH.values()) | set(WINDOWS) | {
    'cpmPacketSize', 'cpmRxObjectCount', 'transmission',
}
PRR_FIELDS = ('eligible', 'received', 'failed', 'censoredDeparture', 'censoredTermination', 'resolved')
COVERAGE_FIELDS = ('eligibleEpisodes', 'eligibleSeconds', 'zeroExposureEpisodes',
    'zeroUpdateEpisodes', 'oneUpdateEpisodes', 'freshUpdates', 'completedIntervals',
    'completedIntervalSeconds', 'completedInitialWaits', 'completedInitialWaitSeconds',
    'censoredInitialWaits', 'censoredGaps', 'censoredSeconds')


def metric_name(name):
    return name[:-7] if name.endswith(':vector') else name


class Moments:
    """Numerically stable online mean/variance (Welford); no observations kept."""
    def __init__(self):
        self.n, self.mean, self.m2, self.total = 0, 0.0, 0.0, 0.0

    def add(self, value):
        self.n += 1
        delta = value - self.mean
        self.mean += delta / self.n
        self.m2 += delta * (value - self.mean)
        self.total += value

    def sd(self):
        return math.sqrt(max(0.0, self.m2 / (self.n - 1))) if self.n > 1 else float('nan')


class Reservoir:
    """Algorithm R: every observation has equal probability of being retained."""
    def __init__(self, cap, seed):
        self.data = np.empty(cap, dtype=np.float64)
        self.n = 0
        self.rng = random.Random(seed)

    def add(self, value):
        self.n += 1
        if self.n <= len(self.data):
            self.data[self.n - 1] = value
        else:
            index = self.rng.randrange(self.n)
            if index < len(self.data):
                self.data[index] = value

    def values(self):
        return self.data[:min(self.n, len(self.data))]


class Summary:
    def __init__(self, cfg, seed):
        self.cfg = cfg
        self.nbins = math.ceil((cfg.end - cfg.start) / cfg.bin_size)
        self.nwindows = math.ceil((cfg.end - cfg.start) / cfg.window_size)
        self.sizes = Reservoir(cfg.sample_cap, seed)
        self.packet_moments = Moments()
        self.fresh = {role: Reservoir(cfg.sample_cap, seed + i + 1) for i, role in enumerate(FRESH)}
        self.channel = [Reservoir(cfg.sample_cap, seed + 3 + i) for i in range(self.nwindows)]
        self.series = {name: [Moments() for _ in range(self.nbins)] for name in SERIES}
        self.perception = {name: Moments() for name in PERCEPTION}
        self.counts = {name: [Moments() for _ in range(self.nwindows)]
                       for name in ('transmission', 'cpmTxObjectCount', 'cpmRxObjectCount')}
        self.packet_bytes = np.zeros(self.nbins)
        self.window_bytes = np.zeros(self.nbins)
        self.exposure = np.zeros(self.nbins)
        self.wall_seconds = np.zeros(self.nbins)
        self.rebin = np.zeros(self.nbins, dtype=bool)
        self.rebin_nonzero = np.zeros(self.nbins, dtype=bool)
        self.accepted = self.nonfinite = self.outside = 0
        self.present, self.scalar_present, self.runs = set(), set(), set()
        self.scalars = {}
        self.warnings = []
        self.sources = {}
        self.window_count = self.zero_windows = self.partial_windows = 0
        self.tx_ready = False
        self.window_total_bytes = self.window_total_seconds = 0.0
        self.tmp = tempfile.TemporaryDirectory(prefix='cpm-plots-', dir=cfg.temp_dir)
        self.db = sqlite3.connect(str(Path(self.tmp.name) / 'windows.sqlite'))
        self.db.execute('PRAGMA cache_size=-2048')
        self.db.execute('PRAGMA temp_store=FILE')
        self.db.execute('CREATE TABLE windows (run TEXT, module TEXT, t REAL, bytes REAL, duration REAL, rate REAL, '
                        'PRIMARY KEY(run,module,t)) WITHOUT ROWID')

    def warn(self, message):
        if message not in self.warnings:
            self.warnings.append(message)

    def index(self, time, size, n):
        # Match accounting's right-closed windows; a boundary submission is in
        # the window ending there. Include the selected initial endpoint too.
        q = (time - self.cfg.start) / size
        nearest = round(q)
        if abs(q-nearest) < 1e-10:
            q = float(nearest)
        return min(max(math.ceil(q)-1, 0), n-1)

    def add(self, name, time, value, run='default', module=''):
        if not math.isfinite(time) or not math.isfinite(value):
            self.nonfinite += 1
            return
        self.runs.add(run)
        if name in WINDOWS:
            # Read one extra second for windows crossing a selected end time.
            if time < self.cfg.start or time > self.cfg.end + 1.000000001:
                self.outside += 1
                return
            if not module:
                self.warn('TX windows require CSV-R module and runID columns; rates unavailable without station identity.')
                return
            column = WINDOWS[name]
            old = self.db.execute(f'SELECT {column} FROM windows WHERE run=? AND module=? AND t=?',
                                  (run,module,time)).fetchone()
            if old and old[0] is not None:
                raise ValueError(f'duplicate {name} at {run}/{module}/{time}')
            self.db.execute(f'INSERT INTO windows(run,module,t,{column}) VALUES(?,?,?,?) '
                            f'ON CONFLICT(run,module,t) DO UPDATE SET {column}=excluded.{column}',
                            (run,module,time,value))
            self.accepted += 1
            return
        if not self.cfg.start <= time <= self.cfg.end:
            self.outside += 1
            return
        self.accepted += 1
        bi = self.index(time, self.cfg.bin_size, self.nbins)
        wi = self.index(time, self.cfg.window_size, self.nwindows)
        if name == 'cpmPacketSize':
            if value <= 0 or value != int(value):
                raise ValueError('cpmPacketSize must contain positive encoded payload bytes; suppressed sends have no samples')
            self.sizes.add(value)
            self.packet_moments.add(value)
            self.packet_bytes[bi] += value
        for role, metric in FRESH.items():
            if name == metric:
                if value <= 0:
                    raise ValueError(f'{metric} must contain strictly positive fresh-information intervals')
                self.fresh[role].add(value)
        if name == 'cpmTxObjectCount' and (value < 0 or value > 255 or value != int(value)):
            raise ValueError('encoded CPM object count outside 0..255')
        if name in self.series:
            self.series[name][bi].add(value)
        if name == 'cpmChannelLoad':
            self.channel[wi].add(value)
        if name in self.counts:
            # transmission values are cumulative. Count events, never their sum.
            self.counts[name][wi].add(1 if name == 'transmission' else value)
        if name in self.perception:
            self.perception[name].add(value)

    def overlaps(self, start, end):
        start, end = max(start,self.cfg.start), min(end,self.cfg.end)
        if end <= start:
            return
        first = max(0, int((start-self.cfg.start)/self.cfg.bin_size))
        last = self.index(end,self.cfg.bin_size,self.nbins)
        for i in range(first,last+1):
            left = self.cfg.start + i*self.cfg.bin_size
            overlap = min(end,left+self.cfg.bin_size,self.cfg.end)-max(start,left)
            if overlap > 1e-12:
                yield i, overlap

    def finish_windows(self):
        bounds = {}
        unpaired = 0
        try:
            self.db.commit()
            for run,module,t,byte_count,duration,rate in self.db.execute('SELECT * FROM windows'):
                if byte_count is None or duration is None:
                    unpaired += 1
                    continue
                if byte_count < 0 or byte_count != int(byte_count) or not 0 < duration <= 1.000000001:
                    raise ValueError(f'invalid TX window at {run}/{module}/{t}')
                if rate is not None and not math.isclose(rate,8*byte_count/duration,rel_tol=1e-9,abs_tol=1e-6):
                    raise ValueError(f'TX rate does not match its encoded bytes/duration at {run}/{module}/{t}')
                start = t-duration
                parts = list(self.overlaps(start,t))
                if not parts:
                    continue
                old = bounds.get(run,(start,t))
                bounds[run] = (min(old[0],start), max(old[1],t))
                self.window_count += 1
                self.zero_windows += byte_count == 0
                self.partial_windows += duration < 1-1e-10
                full = start >= self.cfg.start-1e-10 and t <= self.cfg.end+1e-10
                if full:
                    self.window_total_bytes += byte_count
                    self.window_total_seconds += duration
                for i,seconds in parts:
                    self.exposure[i] += seconds
                    if full and len(parts) == 1:
                        self.window_bytes[i] += byte_count
                    else:
                        self.rebin[i] = True
                        self.rebin_nonzero[i] |= byte_count > 0
            if unpaired:
                self.warn(f'{unpaired} unpaired TX windows: corrected TX rates unavailable; check vector recording.')
            self.tx_ready = bool(self.window_count and not unpaired)
            if self.rebin.any():
                self.warn('Plot boundaries cut recording windows: affected bins use exact encoded packet events and window exposure.')
                if 'cpmPacketSize' not in self.present and self.rebin_nonzero.any():
                    self.warn('Encoded packet events unavailable for nonzero cut windows; affected TX bins are unavailable.')
            for start,end in bounds.values():
                # Network silence before the first station enters still belongs
                # to the requested measurement period. Station exposure does not.
                for i,seconds in self.overlaps(self.cfg.start,end):
                    self.wall_seconds[i] += seconds
                if end < self.cfg.end-1e-10:
                    self.warn('Network exposure stops at the last recorded window; later run-wall exposure is unavailable. Set --end to the recorded run end.')
            if len(bounds)>1:
                self.warn('Multiple runs are pooled: station rates are exposure-weighted; network rates average run-wall exposure. No replicate confidence intervals.')
            self.tx_bytes = np.where(self.rebin,self.packet_bytes,self.window_bytes)
            if 'cpmPacketSize' not in self.present:
                # An all-idle station need never declare a packet-size vector.
                # A cut zero-byte window still has exactly zero traffic.
                self.tx_bytes[self.rebin] = self.window_bytes[self.rebin]
                self.tx_bytes[self.rebin_nonzero] = np.nan
            elif self.tx_ready and not self.rebin.any() and not np.allclose(self.tx_bytes,self.packet_bytes,rtol=0,atol=1e-6):
                raise ValueError('window byte totals disagree with encoded packet events; check missing vectors or measurement boundaries')
            if self.tx_ready and np.any((self.tx_bytes>0)&(self.exposure<=0)):
                raise ValueError('transmitted bytes have no matching station-active exposure')
        finally:
            self.db.close()
            self.tmp.cleanup()

    def rates(self, network=False):
        denominator = self.wall_seconds if network else self.exposure
        if not self.tx_ready:
            return np.full(self.nbins,np.nan)
        return np.divide(8*self.tx_bytes,denominator,out=np.full(self.nbins,np.nan),where=denominator>0)

    def scalar(self, name, value):
        if not math.isfinite(value):
            return  # empty-bin ratio NaNs are expected; counts must be finite
        self.scalar_present.add(name)
        self.scalars[name] = self.scalars.get(name,0.0)+value

    def prr(self, scope):
        names = ['cpmLinkPrr.all.'+scope+'.'+field for field in PRR_FIELDS]
        if not all(name in self.scalar_present for name in names):
            return None
        v = dict(zip(PRR_FIELDS,(self.scalars[name] for name in names)))
        if any(x<0 or x!=int(x) for x in v.values()):
            raise ValueError('PRR opportunity counts must be nonnegative integers')
        if v['resolved'] != v['received']+v['failed'] or v['eligible'] != v['resolved']+v['censoredDeparture']+v['censoredTermination']:
            raise ValueError('corrected PRR opportunity conservation failed')
        v['ratio'] = v['received']/v['resolved'] if v['resolved'] else None
        return v

    def report(self):
        bins = [self.prr(f'distance{d}to{d+25}m') for d in range(0,500,25)]
        coverage = {role:{field:self.scalars.get(f'objectFreshness.cpmOnly.{role}.{field}')
                          for field in COVERAGE_FIELDS} for role in FRESH}
        for role,v in coverage.items():
            if all(v[k] is not None for k in ('eligibleEpisodes','zeroUpdateEpisodes','oneUpdateEpisodes')):
                if v['zeroUpdateEpisodes']+v['oneUpdateEpisodes'] > v['eligibleEpisodes']:
                    raise ValueError(f'{role}: invalid freshness episode coverage')
        count = sum(m.n for m in self.counts['transmission']) if 'transmission' in self.present else None
        objects = sum(m.total for m in self.counts['cpmTxObjectCount']) if 'cpmTxObjectCount' in self.present else None
        if count is not None and 'cpmPacketSize' in self.present and count != self.packet_moments.n:
            raise ValueError('submission event count disagrees with encoded packet sample count')
        if count is not None and 'cpmTxObjectCount' in self.present and count != sum(m.n for m in self.counts['cpmTxObjectCount']):
            raise ValueError('submission event count disagrees with encoded object-count sample count')
        def finite(values):
            return [float(v) if math.isfinite(v) else None for v in values]
        active = float(self.exposure.sum())
        wall = float(self.wall_seconds.sum())
        byte_total = float(self.tx_bytes.sum()) if self.tx_ready and np.isfinite(self.tx_bytes).all() else None
        missing = sorted(TARGETS-self.present)
        if not self.tx_ready: missing.append('corrected TX throughput (no complete byte/duration windows)')
        if self.prr('total') is None: missing.append('cpmLinkPrr.all.total.* scalars')
        if any(v is None for v in bins): missing.append('cpmLinkPrr.all.distance{0..475}to{25..500}m.* scalars (incomplete bins)')
        for role,values in coverage.items():
            if any(v is None for v in values.values()): missing.append('objectFreshness.cpmOnly.'+role+'.* coverage scalars (incomplete)')
        return dict(sources=self.sources,vector_window=[self.cfg.start,self.cfg.end],
            scalar_scope='Whole recorded scalar period; vector time filters do not apply',
            network_period='Selected start through the last recorded window in each run; initial silence included. Select a post-warm-up start.',
            prr_population='All eligible vehicle/RSU transmitter-receiver pairs pooled (VV+VR+RV+RR), TX-RX 0..500 m',
            prr_total=self.prr('total'),prr_25m_bins=bins,
            freshness_population='Vehicle receivers primary; RSU receivers separate; physical object-to-receiver 0<d<=500 m',
            freshness_interval_samples={r:s.n for r,s in self.fresh.items()},freshness_coverage=coverage,
            cdf_sample_cap=self.cfg.sample_cap,
            application_submissions=count,encoded_payload_bytes=self.packet_moments.total if 'cpmPacketSize' in self.present else None,
            encoded_objects=objects,packet_samples=self.packet_moments.n,
            tx_window_samples=self.window_count,idle_window_samples=self.zero_windows,partial_window_samples=self.partial_windows,
            active_station_seconds=active,run_wall_seconds=wall,throughput_payload_bytes=byte_total,
            per_active_station_bit_s=8*byte_total/active if byte_total is not None and active else None,
            network_bit_s=8*byte_total/wall if byte_total is not None and wall else None,
            throughput_bins=dict(bytes=finite(self.tx_bytes),active_station_seconds=finite(self.exposure),
                run_wall_seconds=finite(self.wall_seconds),per_active_station_bit_s=finite(self.rates()),
                network_bit_s=finite(self.rates(True))),
            missing_metrics=missing,warnings=self.warnings)


def read_sca(path, summary):
    # Scalar totals are already aggregated by the observer; never reconstruct
    # PRR from distance vectors or average per-station/per-run ratios.
    seen = set()
    run = 'default'
    with path.open(encoding='utf-8') as stream:
        for lineno,line in enumerate(stream,1):
            if line.startswith('run '):
                run = shlex.split(line)[1]
            elif line.startswith('scalar '):
                _,module,name,value = shlex.split(line)
                if not (name.startswith('cpmLinkPrr.') or name.startswith('objectFreshness.cpmOnly.')):
                    continue
                # ratio is derived from summed counts, not summed/averaged.
                if name.endswith('.ratio'):
                    continue
                key = (run,module,name)
                if key in seen:
                    raise ValueError(f'{path}:{lineno}: duplicate scalar {name}')
                seen.add(key)
                summary.scalar(name,float(value))


def read_vec(path, summary):
    """Read declarations with quoting; honor TV/ETV and other column orders."""
    vectors = {}
    run_count = declarations = 0
    run = "default"
    with path.open('rb') as probe:
        if probe.read(16).startswith(b'SQLite format 3'):
            raise ValueError(f'{path}: SQLite vectors are unsupported; supply text .vec or CSV-R')
    with path.open('r', encoding='utf-8-sig', buffering=1024 * 1024) as stream:
        for lineno, line in enumerate(stream, 1):
            stripped = line.lstrip()
            if not stripped or stripped.startswith('#'):
                continue
            command = stripped.split(None, 1)[0] if stripped.strip() else ''
            if command == 'run':
                run_count += 1
                run = shlex.split(line)[1]
                vectors.clear()  # IDs may be reused between runs.
            elif command == 'vector':
                fields = shlex.split(stripped, comments=True)
                if len(fields) not in (4, 5):
                    raise ValueError(f'{path}:{lineno}: invalid vector declaration')
                declarations += 1
                name = metric_name(fields[3])
                columns = fields[4] if len(fields) == 5 else 'TV'
                vector_id = int(fields[1])
                if name in TARGETS:
                    if set(columns) - set('ETV') or columns.count('T') != 1 or columns.count('V') != 1:
                        raise ValueError(f'{path}:{lineno}: unsupported columns {columns!r}')
                    vectors[vector_id] = (name, columns.index('T') + 1,
                                          columns.index('V') + 1, len(columns) + 1, fields[2])
                    summary.present.add(name)
                else:
                    vectors.pop(vector_id, None)
            elif stripped[0].isdigit():
                # Do not parse numbers for irrelevant vectors.
                fields = stripped.split()
                spec = vectors.get(int(fields[0]))
                if spec is None:
                    continue
                name, ti, vi, length, module = spec
                if len(fields) != length:
                    raise ValueError(f'{path}:{lineno}: incomplete vector data row')
                try:
                    time, value = float(fields[ti]), float(fields[vi])
                except ValueError as exc:
                    raise ValueError(f'{path}:{lineno}: invalid time/value') from exc
                summary.add(name, time, value, run, module)
    if not declarations:
        raise ValueError(f'{path}: no text vector declarations found (SQLite/CSV-S unsupported)')
    if run_count > 1:
        print(f'WARNING: {path.name}: pooling {run_count} runs; these are not replicate CIs.', file=sys.stderr)


CSV_SPECIAL = re.compile('[,"\r\n]')


def csv_rows_spooled(stream, temp_dir=None, chunk_size=65536):
    """Streaming RFC-style comma/quote parser; even one huge row stays bounded.

    Yields seeked field streams, valid until the next row is requested.
    Each field rolls over to disk at 64KiB. Quoted commas/newlines and doubled
    quotes work even across chunk boundaries. No csv.reader giant strings.
    """
    def new_field():
        return tempfile.SpooledTemporaryFile(max_size=65536, mode='w+t',
                                             encoding='utf-8', newline='', dir=temp_dir)
    row, field, state, skip_lf, active = [], new_field(), 'start', False, False

    def put(text):
        nonlocal state, active
        if not text:
            return
        if state == 'after_quote':
            raise ValueError('CSV: characters after closing quote')
        if state == 'start':
            state = 'bare'
        field.write(text)
        active = True

    try:
        while True:
            chunk = stream.read(chunk_size)
            if not chunk:
                break
            last = 0
            for match in CSV_SPECIAL.finditer(chunk):
                segment = chunk[last:match.start()]
                if segment:
                    skip_lf = False
                    put(segment)
                char = match.group()
                last = match.end()
                if skip_lf and char == '\n':
                    skip_lf = False
                    continue
                skip_lf = False
                active = True
                if char == '"':
                    if state == 'start':
                        state = 'quoted'
                    elif state == 'quoted':
                        state = 'after_quote'
                    elif state == 'after_quote':
                        field.write('"')
                        state = 'quoted'
                    else:
                        raise ValueError('CSV: quote inside unquoted field')
                elif state == 'quoted':
                    field.write(char)
                else:
                    field.seek(0)
                    row.append(field)
                    field = new_field()
                    state = 'start'
                    if char != ',':
                        skip_lf = char == '\r'
                        try:
                            yield row
                        finally:
                            for item in row:
                                item.close()
                            row = []
                        active = False
            if last < len(chunk):
                skip_lf = False
                put(chunk[last:])
        if state == 'quoted':
            raise ValueError('CSV: unterminated quoted field')
        if active or row:
            field.seek(0)
            row.append(field)
            field = new_field()
            yield row
    finally:
        field.close()
        for item in row:
            item.close()


def small_field(field):
    value = field.read(65537)
    if len(value) > 65536:
        raise ValueError('CSV: unexpectedly large metadata field')
    return value.strip()


def numeric_tokens(field, chunk_size=65536):
    """Tokenize numeric fields without a full-field read or full-field split."""
    tail = ''
    while True:
        chunk = field.read(chunk_size)
        if not chunk:
            if tail:
                yield float(tail)
            return
        text = tail + chunk
        parts = text.split()
        tail = parts.pop() if text and not text[-1].isspace() and parts else ''
        if len(tail) > 65536:
            raise ValueError('CSV: numeric token longer than 64KiB')
        for part in parts:
            yield float(part)


def read_csv(path, summary):
    with path.open('r', encoding='utf-8-sig', newline='') as stream:
        rows = csv_rows_spooled(stream, summary.cfg.temp_dir)
        try:
            header = next(rows, None)
            if header is None:
                raise ValueError(f'{path}: empty CSV')
            names = [small_field(f) for f in header]
            required = {'type', 'name', 'vectime', 'vecvalue'}
            if not required.issubset(names):
                raise ValueError(f'{path}: expected CSV-R columns {sorted(required)}; use the text .vec instead')
            index = {name: names.index(name) for name in required}
            run_index = names.index('runID') if 'runID' in names else None
            module_index = names.index('module') if 'module' in names else None
            first_run, multiple_runs = None, False
            for rowno, fields in enumerate(rows, 2):
                if len(fields) == 1 and not small_field(fields[0]):
                    continue
                if len(fields) != len(names):
                    raise ValueError(f'{path}: CSV row {rowno} has wrong number of fields')
                if small_field(fields[index['type']]) != 'vector':
                    continue
                name = metric_name(small_field(fields[index['name']]))
                if name not in TARGETS:
                    continue
                summary.present.add(name)
                if run_index is not None:
                    run = small_field(fields[run_index])
                    if first_run is None:
                        first_run = run
                    multiple_runs |= run != first_run
                run = small_field(fields[run_index]) if run_index is not None else 'default'
                module = small_field(fields[module_index]) if module_index is not None and run_index is not None else ''
                try:
                    pairs = zip_longest(numeric_tokens(fields[index['vectime']]),
                                        numeric_tokens(fields[index['vecvalue']]))
                    for time, value in pairs:
                        if time is None or value is None:
                            raise ValueError('vectime and vecvalue lengths differ')
                        summary.add(name, time, value, run, module)
                except ValueError as exc:
                    raise ValueError(f'{path}: CSV row {rowno}: {exc}') from exc
            if multiple_runs:
                print(f'WARNING: {path.name}: pooling multiple runs; these are not replicate CIs.', file=sys.stderr)
        finally:
            rows.close()


def style(index):
    return dict(color=COLORS[index % len(COLORS)], marker=MARKERS[index % len(MARKERS)],
                linestyle=LINESTYLES[index % len(LINESTYLES)], linewidth=2, markersize=5)


def finish(fig, cfg, filename):
    fig.tight_layout()
    try:
        fig.savefig(cfg.output_dir / filename, dpi=cfg.dpi)
        print(f'Saved {filename}', flush=True)
    finally:
        plt.close(fig)


def sample_note(samples):
    approx = any(s.n > len(s.data) for s in samples)
    return 'Uniform reservoir sample (approximate)' if approx else 'All recorded observations'


def make_plots(data, cfg):
    print(f'Vectors: {cfg.start:g}–{cfg.end:g}s. PRR and coverage: whole recorded scalar period.', flush=True)
    plt.rcParams.update({'font.size':11,'axes.labelsize':12,'axes.titlesize':13})
    fig,ax = plt.subplots(figsize=(9,5))
    for i,(name,s) in enumerate(data.items()):
        points = [s.prr(f'distance{d}to{d+25}m') for d in range(0,500,25)]
        if any(v and v['resolved'] for v in points):
            ax.plot(np.arange(12.5,500,25),[100*v['ratio'] if v and v['ratio'] is not None else np.nan for v in points],label=name,**style(i))
    ax.set(title='Link-layer CPM packet reception ratio\nVehicle and RSU pairs pooled',xlabel='Transmitter–receiver distance (m)',
           ylabel='Successful / resolved eligible pairs (%)',xlim=(0,500),ylim=(0,101))
    if ax.lines:
        ax.legend(); ax.grid(alpha=.25)
        finish(fig,cfg,'prr_distance.png')
    else: plt.close(fig)

    sizes = [(name,s.sizes) for name,s in data.items() if s.sizes.n]
    if sizes:
        print('Encoded packet-size boxplots: '+sample_note([r for _,r in sizes]),flush=True)
        fig,ax=plt.subplots(figsize=(9,5))
        boxes=ax.boxplot([r.values() for _,r in sizes],patch_artist=True,showfliers=False)
        for patch,(name,_) in zip(boxes['boxes'],sizes): patch.set_facecolor(COLORS[list(data).index(name)%len(COLORS)])
        ax.set_xticks(range(1,len(sizes)+1),[name for name,_ in sizes],rotation=30)
        ax.set(title='Encoded CPM application-payload size',ylabel='Application payload (bytes)')
        finish(fig,cfg,'packet_size.png')

    for role in FRESH:
        fig,ax=plt.subplots(figsize=(9,5))
        samples=[]
        for i,(name,s) in enumerate(data.items()):
            sample=s.fresh[role]
            if not sample.n: continue
            samples.append(sample)
            x,counts=np.unique(np.sort(sample.values()),return_counts=True)
            ax.step(x,np.cumsum(counts)/counts.sum(),where='post',label=name,
                    color=COLORS[i%len(COLORS)],linestyle=LINESTYLES[i%len(LINESTYLES)])
        ax.set(title=f'CPM-only physical-object fresh-information intervals\n{role.capitalize()} receivers',
               xlabel='Fresh-information arrival interval (s)',ylabel='Conditional cumulative probability',ylim=(0,1.01))
        ax.set_xlim(left=0)
        if samples:
            print(role+' freshness CDF: '+sample_note(samples)+'; completed intervals only. See freshness_coverage.tsv.',flush=True)
            ax.legend(); ax.grid(alpha=.25)
            finish(fig,cfg,f'object_freshness_{role}_cdf.png')
        else: plt.close(fig)

    # Companions expose zero/one-update episodes hidden by an interval-only CDF.
    for role in FRESH:
        entries=[]
        for name,s in data.items():
            v=s.report()['freshness_coverage'][role]
            if all(v[k] is not None for k in ('eligibleEpisodes','zeroUpdateEpisodes','oneUpdateEpisodes')) and v['eligibleEpisodes']>0:
                entries.append((name,v))
        if entries:
            fig,ax=plt.subplots(figsize=(9,5))
            x=np.arange(len(entries)); bottom=np.zeros(len(entries))
            for field,label,color in [('zeroUpdateEpisodes','No fresh update','#d55e00'),('oneUpdateEpisodes','One fresh update','#e69f00')]:
                y=np.array([100*v[field]/v['eligibleEpisodes'] for _,v in entries])
                ax.bar(x,y,bottom=bottom,label=label,color=color); bottom+=y
            ax.bar(x,100-bottom,bottom=bottom,label='At least two updates',color='#009e73')
            ax.set_xticks(x,[name for name,_ in entries],rotation=30)
            ax.set(title=f'Fresh-information coverage: {role} receivers',ylabel='Eligible receiver–object episodes (%)',ylim=(0,100))
            ax.legend(loc='upper left',bbox_to_anchor=(1.01,1)); ax.grid(axis='y',alpha=.25)
            finish(fig,cfg,f'object_freshness_{role}_coverage.png')

    if any(m.n for s in data.values() for m in s.perception.values()):
        fig,ax=plt.subplots(figsize=(9,5))
        for i,(name,s) in enumerate(data.items()):
            points=[(PERCEPTION[k],m.mean) for k,m in s.perception.items() if m.n]
            if points: ax.plot([d for d,_ in points],[v for _,v in points],label=name,**style(i))
        ax.set(title='Mean recorded perception rate',xlabel='Object–receiver distance (m)',ylabel='Perception rate',xlim=(0,500),ylim=(0,1.02))
        ax.legend(); ax.grid(alpha=.25); finish(fig,cfg,'perception_rate.png')

    for metric,(title,ylabel,filename) in SERIES.items():
        if not any(m.n for s in data.values() for m in s.series[metric]): continue
        fig,ax=plt.subplots(figsize=(10,5))
        for i,(name,s) in enumerate(data.items()):
            if not any(m.n for m in s.series[metric]): continue
            scale=cfg.channel_load_scale if metric=='cpmChannelLoad' else 1
            y=[m.mean*scale if m.n else np.nan for m in s.series[metric]]
            x=[min(cfg.end,cfg.start+(j+.5)*cfg.bin_size) for j in range(len(y))]
            ax.plot(x,y,label=name,**style(i))
        if metric=='cpmChannelLoad': ylabel=f'Channel load ({cfg.channel_load_unit})'
        ax.set(title=title,xlabel='Simulation time (s)',ylabel=ylabel,xlim=(cfg.start,cfg.end))
        ax.legend(); ax.grid(alpha=.25); finish(fig,cfg,filename)

    for network in (False,True):
        if not any(s.tx_ready for s in data.values()): continue
        fig,ax=plt.subplots(figsize=(10,5))
        for i,(name,s) in enumerate(data.items()):
            if s.tx_ready:
                edges=np.r_[cfg.start+np.arange(s.nbins)*cfg.bin_size,cfg.end]
                ax.stairs(s.rates(network),edges,baseline=None,label=name,color=COLORS[i%len(COLORS)],linestyle=LINESTYLES[i%len(LINESTYLES)],linewidth=1.8)
        title='Total network CPM application-submission throughput' if network else 'CPM application throughput per active station'
        ax.set(title=title,xlabel='Simulation time (s)',ylabel='Application payload (bit/s)',xlim=(cfg.start,cfg.end),ylim=(0,None))
        ax.legend(); ax.grid(alpha=.25)
        finish(fig,cfg,'tx_throughput_network.png' if network else 'tx_throughput_station.png')

    if any(r.n for s in data.values() for r in s.channel):
        fig,ax=plt.subplots(figsize=(12,5)); width=.8/len(data)
        for i,(name,s) in enumerate(data.items()):
            active=[(j,r) for j,r in enumerate(s.channel) if r.n]
            if not active: continue
            boxes=ax.boxplot([r.values()*cfg.channel_load_scale for _,r in active],
                positions=[j+(i-(len(data)-1)/2)*width for j,_ in active],widths=width*.9,
                patch_artist=True,showfliers=False,manage_ticks=False)
            for patch in boxes['boxes']: patch.set_facecolor(COLORS[i%len(COLORS)])
            ax.plot([],[],color=COLORS[i%len(COLORS)],linewidth=5,label=name)
        n=next(iter(data.values())).nwindows
        ax.set_xticks(range(n),[window_label(cfg,j) for j in range(n)],rotation=30)
        ax.set(title='Recorded channel-load distribution',xlabel='Simulation time window (s)',ylabel=f'Channel load ({cfg.channel_load_unit})')
        ax.legend(loc='upper left',bbox_to_anchor=(1.01,1)); ax.grid(axis='y',alpha=.25)
        finish(fig,cfg,'channel_load_distribution.png')

    for metric,title,ylabel,filename,total in [
        ('transmission','CPM application submissions (including relays)','Application submissions','cpm_submissions.png',False),
        ('cpmTxObjectCount','Perceived objects encoded in submitted CPMs','Encoded object entries','tx_object_count.png',True),
        ('cpmRxObjectCount','Legacy received object-count observations','Observed object entries' if cfg.count_mode=='objects' else 'Observations','legacy_rx_object_count.png',cfg.count_mode=='objects')]:
        entries=[(name,s) for name,s in data.items() if metric in s.present]
        if not entries: continue
        fig,ax=plt.subplots(figsize=(12,5)); width=.8/len(entries)
        for i,(name,s) in enumerate(entries):
            y=[m.total if total else m.n for m in s.counts[metric]]
            ax.bar(np.arange(s.nwindows)+(i-(len(entries)-1)/2)*width,y,width=width*.9,label=name,color=COLORS[list(data).index(name)%len(COLORS)])
        n=entries[0][1].nwindows
        ax.set_xticks(range(n),[window_label(cfg,j) for j in range(n)],rotation=30)
        ax.set(title=title,xlabel='Simulation time window (s)',ylabel=ylabel)
        ax.legend(loc='upper left',bbox_to_anchor=(1.01,1)); ax.grid(axis='y',alpha=.25)
        finish(fig,cfg,filename)


def window_label(cfg, index):
    return f'{cfg.start + index * cfg.window_size:g}–{min(cfg.end, cfg.start + (index + 1) * cfg.window_size):g}'


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--input-dir', type=Path, default=Path('/home/dreamwalker/omnetpp-5.6.2'))
    parser.add_argument('--output-dir', type=Path, default=Path('plots'))
    parser.add_argument('--file', action='append', default=[], metavar='SYSTEM=PATH',
                        help='Repeat for explicit inputs; replaces the default six-file selection.')
    parser.add_argument('--scalar-file', action='append', default=[], metavar='SYSTEM=PATH',
                        help='Corrected .sca companion; otherwise use the vector file sibling.')
    parser.add_argument('--start', type=float, default=300.0)
    parser.add_argument('--end', type=float, default=360.0)
    parser.add_argument('--bin-size', type=float, default=1.0)
    parser.add_argument('--window-size', type=float, default=10.0)
    parser.add_argument('--sample-cap', type=int, default=20000)
    parser.add_argument('--seed', type=int, default=20261009)
    parser.add_argument('--dpi', type=int, default=200)
    parser.add_argument('--temp-dir', type=Path, help='Directory for CSV field spill files.')
    parser.add_argument('--count-mode', choices=('events', 'objects'), default='events')
    parser.add_argument('--channel-load-scale', type=float, default=1.0)
    parser.add_argument('--channel-load-unit', default='recorded units')
    cfg = parser.parse_args(argv)
    if not all(math.isfinite(v) for v in (cfg.start, cfg.end, cfg.bin_size, cfg.window_size, cfg.channel_load_scale)):
        parser.error('numeric options must be finite')
    if cfg.start < 0 or cfg.end <= cfg.start or min(cfg.bin_size, cfg.window_size) <= 0:
        parser.error('require 0 <= start < end, and positive bin/window sizes')
    if cfg.sample_cap < 1 or cfg.dpi < 1:
        parser.error('sample-cap and dpi must be positive')
    if max((cfg.end - cfg.start) / cfg.bin_size, (cfg.end - cfg.start) / cfg.window_size) > 10000:
        parser.error('too many bins/windows; increase bin-size or window-size')
    return cfg


def input_map(entries, option):
    result={}
    for entry in entries:
        name,sep,path=entry.partition('=')
        if not sep or not name or not path or name in result:
            raise ValueError(option+' must be a unique SYSTEM=PATH')
        result[name]=Path(path)
    return result


def write_report(data,cfg):
    report={name:s.report() for name,s in data.items()}
    (cfg.output_dir/'measurement_summary.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    with (cfg.output_dir/'freshness_coverage.tsv').open('w',newline='') as stream:
        writer=csv.writer(stream,delimiter='\t')
        writer.writerow(['method','receiver_population','scalar_scope',*COVERAGE_FIELDS])
        for name,s in report.items():
            for role,v in s['freshness_coverage'].items():
                writer.writerow([name,role,'whole recorded scalar period',*[v[f] if v[f] is not None else 'unavailable' for f in COVERAGE_FIELDS]])
    for name,s in report.items():
        print(name+': application submissions='+str(s['application_submissions'])+
              ', encoded bytes='+str(s['encoded_payload_bytes'])+', active-station seconds='+str(s['active_station_seconds']),flush=True)
        for warning in s['warnings']: print('WARNING: '+name+': '+warning,file=sys.stderr)
        if s['missing_metrics']: print('UNAVAILABLE: '+name+': '+', '.join(s['missing_metrics']),file=sys.stderr)
    return report


def main(argv=None):
    cfg=arguments(argv)
    inputs=input_map(cfg.file,'--file')
    scalar_inputs=input_map(cfg.scalar_file,'--scalar-file')
    if not inputs:
        for name,filename in BASELINES.items():
            path=cfg.input_dir/filename
            if not path.is_file(): path=path.with_suffix('.csv')
            inputs[name]=path
    if set(scalar_inputs)-set(inputs): raise ValueError('--scalar-file method must also be selected by --file/default inputs')
    data={}
    for i,(name,path) in enumerate(inputs.items()):
        if not path.is_file():
            print(f'UNAVAILABLE: missing {name}: {path}',file=sys.stderr)
            continue
        print(f'Reading {name}: {path}',flush=True)
        summary=Summary(cfg,cfg.seed+i*100003)
        summary.sources['vectors']=str(path.resolve()) if path.suffix.lower()!='.sca' else None
        try:
            if path.suffix.lower()=='.csv': read_csv(path,summary)
            elif path.suffix.lower()!='.sca': read_vec(path,summary)
            scalar_path=scalar_inputs.get(name,path.with_suffix('.sca'))
            summary.sources['scalars']=str(scalar_path.resolve()) if scalar_path.is_file() else None
            if scalar_path.is_file(): read_sca(scalar_path,summary)
            else: summary.warn('Corrected scalar companion unavailable: '+str(scalar_path))
            summary.finish_windows()
        except Exception:
            summary.db.close(); summary.tmp.cleanup()
            raise
        # Keep declared but empty metrics: no updates is a result, not a reason
        # to drop an algorithm from availability/coverage reports.
        data[name]=summary
    if not data: raise ValueError('No result files found; check --input-dir/--file paths.')
    cfg.output_dir.mkdir(parents=True,exist_ok=True)
    write_report(data,cfg)
    make_plots(data,cfg)
    print('Done: '+str(cfg.output_dir.resolve()),flush=True)
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,OSError,sqlite3.Error) as exc:
        print(f'ERROR: {exc}',file=sys.stderr)
        raise SystemExit(1)
