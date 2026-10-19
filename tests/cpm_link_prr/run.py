#!/usr/bin/env python3
"""Build/run controlled real-radio PRR fixtures, or stage a Road 120 integration."""
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
parser.add_argument('--output', type=Path, default=Path('/tmp/artery-prr-validation'))
parser.add_argument('--build-only', action='store_true')
parser.add_argument('--case')
parser.add_argument('--method', choices=['Proposed','Static','ETSI','LookAhead','VPCast','wPPersistence'])
parser.add_argument('--until', default='302.1s')
parser.add_argument('--disable-observer', action='store_true')
parser.add_argument('--dcc-probe', action='store_true', help='test-only aggregate release/drop counters')
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
config = ConfigParser()
config.read(ROOT / 'build/run-artery.ini')
library = args.output / 'libprr_fixture.so'
if args.build_only or (args.case and not library.exists()) or args.dcc_probe:
    flags=(ROOT/'build/CMakeFiles/core.dir/flags.make').read_text().splitlines()
    includes=shlex.split(next(s.split('=',1)[1] for s in flags if s.startswith('CXX_INCLUDES =')))
    simlib=Path(config['Executables']['oppRun']).resolve().parents[1]/'lib'
    sources=[('Fixture.cc',library)]
    if args.dcc_probe: sources=[('DccProbe.cc',args.output/'libprr_dcc_probe.so')]
    for source,output in sources:
        subprocess.run(['c++','-std=c++17','-O0','-g','-fPIC','-shared','-fno-access-control',*includes,str(SOURCE/source),
            '-o',str(output),'-ldl','-Wl,--no-as-needed','-L'+str(ROOT/'build/src/artery/envmod'),'-lartery_envmod',
            '-L'+str(ROOT/'build'),'-lartery_core','-L'+str(simlib),'-loppsim','-loppenvir',
            '-Wl,-rpath,'+str(ROOT/'build/src/artery/envmod')+':'+str(ROOT/'build')+':'+str(simlib)],check=True)
if args.build_only:
    raise SystemExit(0)
if bool(args.case) == bool(args.method):
    parser.error('specify --case or --method')
label=('fixture-'+args.case) if args.case else ('road120-'+args.method)
if args.disable_observer: label += '-observer-disabled'
if args.dcc_probe: label += '-dcc-probe'
work=args.output/label
work.mkdir(exist_ok=True)
if args.case:
    shutil.copy2(SOURCE/'fixture.ini',work/'omnetpp.ini')
else:
    for path in (ROOT/'scenarios/road120').iterdir():
        if path.is_file() and path.suffix in ['.xml','.sumocfg','.ini']:
            shutil.copy2(path,work/path.name)
libraries=list(dict.fromkeys(shlex.split(config['Artery']['libraries'])))
libraries.sort(key=lambda s:0 if 'libartery_envmod.so' in s else 1)
if args.case: libraries.insert(0,'-l'+str(library))
if args.dcc_probe: libraries.insert(0,'-l'+str(args.output/'libprr_dcc_probe.so'))
command=[config['Executables']['oppRun'],'-u','Cmdenv','-p','-',
    '-n',config['Artery']['nedFolders']+':'+str(SOURCE),*libraries,
    '-f',str(work/'omnetpp.ini'),'-c',args.case or args.method,
    '--cmdenv-interactive=false','--print-undisposed=false','--result-dir='+str(work/'results')]
if args.method: command += ['--sim-time-limit='+args.until]
if args.dcc_probe: command += ['--**.testDcc*.scalar-recording=true']
if args.disable_observer: command += ['--*.withObserver=false' if args.case else '--*.withCpmLinkPrr=false']
(work/'command.txt').write_text(shlex.join(command)+'\n')
with (work/'simulation.log').open('w') as log:
    run=subprocess.run(command,cwd=work,env=os.environ.copy(),stdout=log,stderr=subprocess.STDOUT,timeout=600)
print(label,'exit:',run.returncode,'output:',work)
print((work/'simulation.log').read_text()[-1400:])
raise SystemExit(run.returncode)
