#!/usr/bin/env python3
"""Controlled checks for corrected plot inputs; no simulation or legacy fallback."""
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('masterplot',ROOT/'scripts/masterplot.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


def vectors():
    rows=[]
    def add(module,name,values): rows.append((module,name,values))
    a,b,c='World.node[0].middleware.CP','World.rsu0.middleware.CP','World.node[1].middleware.CP'
    for module,windows in [(a,[(1,100,.5),(2,0,1),(3,500,1),(3.25,50,.25)]),
                           (b,[(1,0,.5),(2,0,1),(3,0,1),(3.25,0,.25)]),
                           (c,[(2,200,.75),(2.75,0,.75)])]:
        # Reverse metric order and group data blocks: joins cannot depend on
        # vector IDs, adjacency, or arrival order in the result file.
        add(module,'cpmTxWindowDuration',[(t,d) for t,byte,d in windows])
        add(module,'cpmTxThroughput',[(t,8*byte/d) for t,byte,d in windows])
        add(module,'cpmTxWindowBytes',[(t,byte) for t,byte,d in windows])
    for module,packets in [(a,[(.5,100,1),(2.5,200,2),(3,300,255),(3.25,50,0)]),(c,[(1.5,200,2)])]:
        add(module,'cpmPacketSize',[(t,byte) for t,byte,obj in packets])
        add(module,'cpmTxObjectCount',[(t,obj) for t,byte,obj in packets])
        # Deliberately large cumulative values: summing these is incorrect.
        add(module,'transmission',[(t,100+i) for i,(t,byte,obj) in enumerate(packets)])
    add('World.cpmObjectFreshness',p.FRESH['vehicle'],[(1,.1),(1.2,.2),(1.5,.2),(2,.4)])
    add('World.cpmObjectFreshness',p.FRESH['rsu'],[(1,.3),(2,.6)])
    add(a,'cpmObjectUpdateInterval',[(1,9000)])
    add(a,'cpmExpectedDistance',[(1,10)])
    add(a,'cpmReceivedDistance',[(1,10)]*99)
    add(a,'cpmChannelLoad',[(1,.25),(2,.5)])
    add(a,'cpmPerceptionRate25',[(1,.75)])
    add(a,'cpmRxObjectCount',[(1,2)])
    return rows


def write_vec(path,rows,run='test',append=False):
    with path.open('a' if append else 'w') as f:
        f.write('version 2\nrun '+run+'\n')
        for i,(module,name,values) in enumerate(rows):
            f.write(f'vector {i} "{module}" {name}:vector ETV\n')
        for i,(module,name,values) in reversed(list(enumerate(rows))):
            for event,(t,v) in enumerate(values): f.write(f'{i}\t{event}\t{t}\t{v}\n')


def write_sca(path):
    with path.open('w') as f:
        f.write('version 2\nrun test\n')
        totals=dict.fromkeys(p.PRR_FIELDS,0)
        for d in range(0,500,25):
            values=dict.fromkeys(p.PRR_FIELDS,0)
            if d==0: values.update(eligible=8,received=3,failed=1,censoredDeparture=2,censoredTermination=2,resolved=4)
            if d==475: values.update(eligible=3,received=2,failed=1,resolved=3)
            for field,value in values.items():
                totals[field]+=value
                f.write(f'scalar World.cpmLinkPrr cpmLinkPrr.all.distance{d}to{d+25}m.{field} {value}\n')
            f.write(f'scalar World.cpmLinkPrr cpmLinkPrr.all.distance{d}to{d+25}m.ratio nan\n')
        for field,value in totals.items(): f.write(f'scalar World.cpmLinkPrr cpmLinkPrr.all.total.{field} {value}\n')
        # A conflicting legacy ratio must have no influence.
        f.write('scalar World.node[0].middleware.CP cpmPrr:mean 99\n')
        for role,episodes,zero,one in [('vehicle',10,4,2),('rsu',5,1,1)]:
            v=dict.fromkeys(p.COVERAGE_FIELDS,0)
            v.update(eligibleEpisodes=episodes,zeroExposureEpisodes=1,zeroUpdateEpisodes=zero,oneUpdateEpisodes=one,
                     completedIntervals=8,completedIntervalSeconds=1.6,completedInitialWaits=episodes-zero,
                     completedInitialWaitSeconds=2,censoredInitialWaits=zero,censoredGaps=episodes-zero,
                     censoredSeconds=3,eligibleSeconds=6.6,freshUpdates=episodes-zero+8)
            for field,value in v.items(): f.write(f'scalar World.cpmObjectFreshness objectFreshness.cpmOnly.{role}.{field} {value}\n')


class CorrectedMeasurements(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='masterplot-test-')
        self.dir=Path(self.tmp.name)
        self.vec=self.dir/'test.vec'; self.sca=self.vec.with_suffix('.sca')
        write_vec(self.vec,vectors()); write_sca(self.sca)
        self.cfg=p.arguments(['--start','0','--end','3.25','--sample-cap','3','--output-dir',str(self.dir/'plots')])
    def tearDown(self): self.tmp.cleanup()
    def load(self,cfg=None,rows=None,scalar=True):
        if rows is not None: write_vec(self.vec,rows)
        s=p.Summary(cfg or self.cfg,42)
        try:
            p.read_vec(self.vec,s)
            if scalar: p.read_sca(self.sca,s)
            s.finish_windows()
            return s
        except Exception:
            s.db.close();s.tmp.cleanup()
            raise
    def test_resolved_prr_and_censored_denominator(self):
        s=self.load(); r=s.report()
        self.assertAlmostEqual(r['prr_total']['ratio'],5/7)
        self.assertEqual(s.prr('distance0to25m')['ratio'],.75)
        self.assertEqual(s.prr('distance25to50m')['ratio'],None)
        self.assertEqual(len(r['prr_25m_bins']),20)
        self.assertEqual(r['prr_total']['eligible'],11)
    def test_encoded_packets_submissions_and_objects(self):
        r=self.load().report()
        self.assertEqual(r['application_submissions'],5)
        self.assertEqual(r['encoded_payload_bytes'],850)
        self.assertEqual(r['encoded_objects'],260) # includes an encoded 255-entry packet
    def test_idle_partial_and_active_station_weighting(self):
        r=self.load().report()
        self.assertEqual(r['idle_window_samples'],6)
        self.assertEqual(r['partial_window_samples'],6)
        self.assertEqual(r['tx_window_samples'],10)
        self.assertAlmostEqual(r['active_station_seconds'],7)
        self.assertAlmostEqual(r['run_wall_seconds'],3.25)
        self.assertAlmostEqual(r['per_active_station_bit_s'],8*850/7)
        self.assertAlmostEqual(r['network_bit_s'],8*850/3.25)
        self.assertEqual(r['throughput_bins']['bytes'],[100,200,500,50])
    def test_cut_windows_rebin_without_byte_interpolation(self):
        self.cfg.start=.5
        s=self.load();r=s.report()
        self.assertTrue(s.rebin.any())
        self.assertEqual(r['throughput_bins']['bytes'],[300,200,350])
        self.assertAlmostEqual(r['throughput_payload_bytes'],850)
        self.assertAlmostEqual(r['active_station_seconds'],7)
    def test_fresh_vehicle_and_rsu_separate_no_legacy(self):
        s=self.load()
        self.assertEqual(s.fresh['vehicle'].n,4)
        self.assertEqual(s.fresh['rsu'].n,2)
        self.assertTrue(all(x<1 for x in s.fresh['vehicle'].values()))
        self.assertEqual(s.report()['freshness_coverage']['vehicle']['zeroUpdateEpisodes'],4)
        self.assertEqual(len(s.fresh['vehicle'].values()),3) # bounded uniform reservoir
    def test_missing_corrected_metrics_do_not_fallback(self):
        s=self.load(rows=[r for r in vectors() if r[1] not in p.WINDOWS and r[1] not in p.FRESH.values()],scalar=False)
        r=s.report()
        self.assertIsNone(r['prr_total'])
        self.assertFalse(s.tx_ready)
        self.assertEqual(r['freshness_interval_samples']['vehicle'],0)
        self.assertIn(p.FRESH['vehicle'],r['missing_metrics'])
    def test_idle_only_cut_windows_without_packet_vectors(self):
        self.cfg.start=.75
        rows=[r for r in vectors() if r[0]=='World.rsu0.middleware.CP']
        s=self.load(rows=rows)
        self.assertTrue(s.tx_ready)
        self.assertEqual(list(s.rates()),[0,0,0])
        self.assertAlmostEqual(s.report()['active_station_seconds'],2.5)
    def test_cut_nonzero_windows_require_encoded_events(self):
        self.cfg.start=.75
        s=self.load(rows=[r for r in vectors() if r[1]!='cpmPacketSize'])
        self.assertTrue(s.rebin_nonzero.any())
        self.assertIsNone(s.report()['throughput_payload_bytes'])
    def test_incomplete_windows_unavailable(self):
        s=self.load(rows=[r for r in vectors() if r[1]!='cpmTxWindowDuration'])
        self.assertFalse(s.tx_ready)
        self.assertTrue(any('unpaired' in w for w in s.warnings))
    def test_invalid_prr_counts_rejected(self):
        s=self.load();s.scalars['cpmLinkPrr.all.total.resolved']=8
        with self.assertRaises(ValueError): s.report()
    def test_zero_packet_sample_rejected(self):
        with self.assertRaises(ValueError): self.load(rows=[('station','cpmPacketSize',[(1,0)])])
    def test_256_encoded_objects_rejected(self):
        with self.assertRaises(ValueError): self.load(rows=[('station','cpmTxObjectCount',[(1,256)])])
    def test_csv_r_streaming_equivalent(self):
        path=self.dir/'test.csv'
        with path.open('w',newline='') as f:
            w=csv.writer(f);w.writerow(['runID','type','module','name','vectime','vecvalue'])
            for module,name,values in vectors():
                w.writerow(['test','vector',module,name+':vector',' '.join(str(t) for t,v in values),' '.join(str(v) for t,v in values)])
        s=p.Summary(self.cfg,42);p.read_csv(path,s);p.read_sca(self.sca,s);s.finish_windows()
        self.assertEqual(s.report(),self.load().report())
    def test_csv_large_field_spooled(self):
        text='type,name,vectime,vecvalue\nvector,cpmPhysicalObjectFreshUpdateIntervalVehicle,"'+'1 '*40000+'","'+'.2 '*40000+'"\n'
        path=self.dir/'large.csv';path.write_text(text)
        s=p.Summary(self.cfg,42);p.read_csv(path,s);s.finish_windows()
        self.assertEqual(s.fresh['vehicle'].n,40000)
        self.assertEqual(len(s.fresh['vehicle'].values()),3)
    def test_multiple_runs_pool_counts_but_average_network_rate(self):
        write_vec(self.vec,vectors(),run='second',append=True)
        r=self.load().report()
        self.assertEqual(r['application_submissions'],10)
        self.assertEqual(r['encoded_payload_bytes'],1700)
        self.assertAlmostEqual(r['per_active_station_bit_s'],8*850/7)
        self.assertAlmostEqual(r['network_bit_s'],8*850/3.25)
    def test_all_six_methods_figures_and_report(self):
        inputs=[]
        for name in p.BASELINES:
            target=self.dir/(name+'.vec');write_vec(target,vectors());write_sca(target.with_suffix('.sca'))
            inputs+=['--file',name+'='+str(target)]
        self.assertEqual(p.main([*inputs,'--start','0','--end','3.25','--sample-cap','3','--dpi','60','--output-dir',str(self.cfg.output_dir)]),0)
        reports=json.loads((self.cfg.output_dir/'measurement_summary.json').read_text())
        self.assertEqual(set(reports),set(p.BASELINES))
        for name,r in reports.items(): self.assertEqual(r['application_submissions'],5)
        for file in ['prr_distance.png','object_freshness_vehicle_cdf.png','object_freshness_rsu_cdf.png',
                     'tx_throughput_station.png','tx_throughput_network.png','cpm_submissions.png']:
            self.assertGreater((self.cfg.output_dir/file).stat().st_size,1000)


if __name__=='__main__': unittest.main(verbosity=2)
