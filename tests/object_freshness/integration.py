#!/usr/bin/env python3
"""Stage Road 120 freshness runs with verified envmod-first library loading."""
from pathlib import Path
from configparser import ConfigParser
import argparse
import json
import os
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path('/tmp/artery-object-freshness/integration'))
parser.add_argument('--method', choices=['Proposed','Static','ETSI','LookAhead','VPCast','wPPersistence'], required=True)
parser.add_argument('--until', default='302.1s')
parser.add_argument('--disabled', action='store_true', help='same topology, observer disabled; no metadata registry')
parser.add_argument('--accounting-probe', type=Path)
parser.add_argument('--benchmark-probe', type=Path)
parser.add_argument('--repeat', default='', help='label repeated overhead runs')
args = parser.parse_args()
config = ConfigParser(); config.read(ROOT/'build/run-artery.ini')
label = 'road120-'+args.method+('-disabled' if args.disabled else '')+('-'+args.repeat if args.repeat else '')
work = args.output.resolve()/label; work.mkdir(parents=True, exist_ok=True)
for path in (ROOT/'scenarios/road120').iterdir():
    if path.is_file() and path.suffix in ['.xml','.sumocfg','.ini']:
        shutil.copy2(path,work/path.name)
libraries = list(dict.fromkeys(shlex.split(config['Artery']['libraries'])))
libraries.sort(key=lambda s: 0 if 'libartery_envmod.so' in s else 1)
if args.accounting_probe: libraries.insert(0,'-l'+str(args.accounting_probe.resolve()))
if args.benchmark_probe: libraries.insert(0,'-l'+str(args.benchmark_probe.resolve()))
command = [config['Executables']['oppRun'],'-u','Cmdenv','-p','-',
    '-n',config['Artery']['nedFolders'],*libraries,'-f',str(work/'omnetpp.ini'),'-c',args.method,
    '--cmdenv-interactive=false','--print-undisposed=false','--sim-time-limit='+args.until,
    '--result-dir='+str(work/'results')]
if args.disabled: command.append('--*.cpmObjectFreshness.enabled=false')
(work/'command.txt').write_text(shlex.join(command)+'\n')
env = os.environ.copy()
if args.accounting_probe: env['CPM_ACCOUNTING_TRACE'] = str(work/'trace.jsonl')
with (work/'simulation.log').open('w') as log:
    run = subprocess.run(['/usr/bin/time','-f','%e %M','-o',str(work/'resource.txt'),*command],
        cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
if run.returncode:
    print((work/'simulation.log').read_text()[-4000:]); raise SystemExit(run.returncode)
seconds,kib = (work/'resource.txt').read_text().split()
print(json.dumps({'run':label,'seconds':float(seconds),'peakRssKiB':int(kib),'output':str(work)}),flush=True)
