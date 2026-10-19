#!/usr/bin/env python3
"""Run controlled production weighted p-persistence forwarding and accounting."""
from pathlib import Path
from configparser import ConfigParser
import argparse
import os
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path('/tmp/artery-weighted-validation'))
parser.add_argument('--case')
args = parser.parse_args()
args.output = args.output.resolve()
args.output.mkdir(parents=True, exist_ok=True)
config = ConfigParser()
config.read(ROOT / 'build/run-artery.ini')
flags = (ROOT / 'build/CMakeFiles/core.dir/flags.make').read_text().splitlines()
includes = shlex.split(next(s.split('=', 1)[1] for s in flags if s.startswith('CXX_INCLUDES =')))
simlib = Path(config['Executables']['oppRun']).resolve().parents[1] / 'lib'
library = args.output / 'libweighted_fixture.so'
subprocess.run(['c++', '-std=c++17', '-O0', '-g', '-fPIC', '-shared', '-fno-access-control', *includes,
    str(SOURCE / 'Fixture.cc'), '-o', str(library), '-ldl', '-Wl,--no-as-needed',
    '-L'+str(ROOT / 'build/src/artery/envmod'), '-lartery_envmod', '-L'+str(ROOT / 'build'), '-lartery_core',
    '-L'+str(simlib), '-loppsim', '-loppenvir',
    '-Wl,-rpath,'+str(ROOT / 'build/src/artery/envmod')+':'+str(ROOT / 'build')+':'+str(simlib)], check=True)
libraries = list(dict.fromkeys(shlex.split(config['Artery']['libraries'])))
libraries.sort(key=lambda s: 0 if 'libartery_envmod.so' in s else 1)
cases = [args.case] if args.case else [
    'probability', 'zero', 'equalProbability', 'minimum', 'geometry', 'dateline', 'equator',
    'staticPosition', 'identity', 'cancel', 'own', 'expiryInitial', 'expiryFallback', 'expiryExact',
    'late', 'short', 'expired', 'future', 'cleanup', 'departure', 'stress', 'fallbackStress', 'randomStress']
for case in cases:
    work = args.output / case
    work.mkdir(exist_ok=True)
    command = [config['Executables']['oppRun'], '-u', 'Cmdenv', '-p', '-',
        '-n', config['Artery']['nedFolders']+':'+str(SOURCE), '-l'+str(library), *libraries,
        '-f', str(SOURCE / 'fixture.ini'), '-c', case, '--result-dir='+str(work / 'results'), '--print-undisposed=true']
    (work / 'command.txt').write_text(shlex.join(command)+'\n')
    env = os.environ.copy()
    # opp_run already links liboppsim, so preload the fixture to inject exact
    # uniform() boundary results before that symbol binds. Production delegates
    # still consume one original RNG sample, and real runs never load this hook.
    env['LD_PRELOAD'] = str(library) + (':'+env['LD_PRELOAD'] if env.get('LD_PRELOAD') else '')
    with (work / 'simulation.log').open('w') as log:
        result = subprocess.run(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    if result.returncode:
        print((work / 'simulation.log').read_text()[-4000:])
        raise SystemExit(result.returncode)
    scalars = '\n'.join(p.read_text() for p in (work / 'results').glob('*.sca'))
    assert 'weightedTestPassed 1' in scalars, case
    assert 'undisposed object:' not in (work / 'simulation.log').read_text().lower(), case
    print(case, 'PASS')
