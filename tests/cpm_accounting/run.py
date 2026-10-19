#!/usr/bin/env python3
"""Build the observer and run headless CPM accounting checks; outputs stay outside scenarios."""
from pathlib import Path
from configparser import ConfigParser
import argparse
import os
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path('/tmp/artery-cpm-accounting'))
parser.add_argument('--build-only', action='store_true')
parser.add_argument('--fixture', action='store_true')
parser.add_argument('--method', choices=['Proposed','Static','ETSI','LookAhead','VPCast','wPPersistence'])
parser.add_argument('--until', default='302.1s')
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
config = ConfigParser()
config.read(ROOT / 'build/run-artery.ini')
observer = args.output / 'libaccounting_probe.so'
if args.build_only or not observer.exists():
    flags=(ROOT/'build/CMakeFiles/core.dir/flags.make').read_text().splitlines()
    includes=shlex.split(next(s.split('=',1)[1] for s in flags if s.startswith('CXX_INCLUDES =')))
    # Resolve original methods through explicit dependencies, even with --as-needed defaults.
    command=['c++','-std=c++17','-O0','-g','-fPIC','-shared','-fno-access-control',*includes,
        str(SOURCE/'Probe.cc'),str(SOURCE/'Fixture.cc'),'-o',str(observer.with_suffix('.building.so')),'-ldl','-Wl,--no-as-needed',
        '-L'+str(ROOT/'build/src/artery/envmod'),'-lartery_envmod','-L'+str(ROOT/'build'),'-lartery_core',
        '-L'+str(Path(config['Executables']['oppRun']).resolve().parents[1]/'lib'),'-loppsim','-loppenvir',
        '-Wl,-rpath,'+str(ROOT/'build/src/artery/envmod')+':'+str(ROOT/'build')+':'+str(Path(config['Executables']['oppRun']).resolve().parents[1]/'lib')]
    subprocess.run(command,check=True)
    observer.with_suffix('.building.so').replace(observer)
if args.build_only:
    raise SystemExit(0)
if not args.fixture and not args.method:
    parser.error('specify --fixture, --method, or --build-only')
label='fixture' if args.fixture else 'road120-'+args.method
work=args.output/label
work.mkdir(exist_ok=True)
if args.fixture:
    shutil.copy2(SOURCE/'fixture.ini',work/'omnetpp.ini')
else:
    for path in (ROOT/'scenarios/road120').iterdir():
        if path.is_file() and path.suffix in ['.xml','.sumocfg','.ini']:
            shutil.copy2(path,work/path.name)
libraries=list(dict.fromkeys(shlex.split(config['Artery']['libraries'])))
libraries.sort(key=lambda s:0 if 'libartery_envmod.so' in s else 1)
libraries.insert(0,'-l'+str(observer))
command=[config['Executables']['oppRun'],'-u','Cmdenv','-p','-',
    '-n',config['Artery']['nedFolders']+':'+str(SOURCE),*libraries,
    '-f',str(work/'omnetpp.ini'),'--cmdenv-interactive=false','--print-undisposed=false',
    '--result-dir='+str(work/'results')]
if args.method:
    command.extend(['-c',args.method,'--sim-time-limit='+args.until])
env=os.environ.copy()
env['CPM_ACCOUNTING_TRACE']=str(work/'trace.jsonl')
(work/'command.txt').write_text(shlex.join(command)+'\n')
with (work/'simulation.log').open('w') as log:
    run=subprocess.run(command,cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(label,'exit:',run.returncode,'output:',work)
print((work/'simulation.log').read_text()[-1800:])
raise SystemExit(run.returncode)
