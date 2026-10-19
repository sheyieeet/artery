#!/usr/bin/env python3
"""Compile and run deterministic production physical-object freshness tests."""
from pathlib import Path
from configparser import ConfigParser
import argparse
import os
import shlex
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path('/tmp/artery-object-freshness/controlled'))
parser.add_argument('--case')
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
config = ConfigParser()
config.read(ROOT / 'build/run-artery.ini')
flags = (ROOT / 'build/CMakeFiles/core.dir/flags.make').read_text().splitlines()
includes = shlex.split(next(s.split('=', 1)[1] for s in flags if s.startswith('CXX_INCLUDES =')))
simlib = Path(config['Executables']['oppRun']).resolve().parents[1] / 'lib'
library = args.output / 'libfreshness_fixture.so'
subprocess.run(['c++', '-std=c++17', '-O0', '-g', '-fPIC', '-shared', '-fno-access-control', *includes,
    str(SOURCE / 'Fixture.cc'), '-o', str(library), '-Wl,--no-as-needed',
    '-L'+str(ROOT / 'build/src/artery/envmod'), '-lartery_envmod', '-L'+str(ROOT / 'build'), '-lartery_core',
    '-L'+str(simlib), '-loppsim', '-loppenvir',
    '-Wl,-rpath,'+str(ROOT / 'build/src/artery/envmod')+':'+str(ROOT / 'build')+':'+str(simlib)], check=True)
libraries = list(dict.fromkeys(shlex.split(config['Artery']['libraries'])))
libraries.sort(key=lambda s: 0 if 'libartery_envmod.so' in s else 1)
cases = [args.case] if args.case else ['identity','multiReporter','duplicate','outOfOrder','local',
    'range','lifecycle','missing','repack','encoders','warmup','idle','stress','roles','receiveHook','boundary','termination','snapshots']
for case in cases:
    work = args.output / case
    work.mkdir(exist_ok=True)
    command = [config['Executables']['oppRun'], '-u', 'Cmdenv', '-p', '-',
        '-n', config['Artery']['nedFolders']+':'+str(SOURCE), '-l'+str(library), *libraries,
        '-f', str(SOURCE / 'fixture.ini'), '-c', case, '--result-dir='+str(work / 'results'), '--print-undisposed=true']
    (work / 'command.txt').write_text(shlex.join(command)+'\n')
    start = time.monotonic()
    with (work / 'simulation.log').open('w') as log:
        result = subprocess.run(command, cwd=work, env=os.environ.copy(), stdout=log, stderr=subprocess.STDOUT, timeout=120)
    if result.returncode:
        print((work / 'simulation.log').read_text()[-5000:])
        raise SystemExit(result.returncode)
    scalars = '\n'.join(p.read_text() for p in (work / 'results').glob('*.sca'))
    assert 'objectFreshnessTestPassed 1' in scalars, case
    assert 'undisposed object:' not in (work / 'simulation.log').read_text().lower(), case
    print(case, 'PASS', f'{time.monotonic()-start:.3f}s', flush=True)
