#!/usr/bin/env python3
"""Serial same-topology observer on/off measurements and RNG/accounting comparisons."""
from pathlib import Path
from configparser import ConfigParser
from collections import defaultdict
import argparse
import json
import math
import shlex
import statistics
import subprocess

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,default=Path('/tmp/artery-object-freshness/benchmark'))
parser.add_argument('--method',default='Proposed')
parser.add_argument('--repeats',type=int,default=3)
parser.add_argument('--until',default='301.1s')
args=parser.parse_args(); args.output=args.output.resolve(); args.output.mkdir(parents=True,exist_ok=True)
config=ConfigParser(); config.read(ROOT/'build/run-artery.ini')
flags=(ROOT/'build/CMakeFiles/core.dir/flags.make').read_text().splitlines()
includes=shlex.split(next(s.split('=',1)[1] for s in flags if s.startswith('CXX_INCLUDES =')))
simlib=Path(config['Executables']['oppRun']).resolve().parents[1]/'lib'
library=args.output/'libfreshness_overhead.so'
subprocess.run(['c++','-std=c++17','-O0','-g','-fPIC','-shared',*includes,
    str(Path(__file__).with_name('OverheadProbe.cc')),'-o',str(library),'-ldl','-Wl,--no-as-needed',
    '-L'+str(ROOT/'build/src/artery/envmod'),'-lartery_envmod','-L'+str(ROOT/'build'),'-lartery_core',
    '-L'+str(simlib),'-loppsim','-loppenvir',
    '-Wl,-rpath,'+str(ROOT/'build/src/artery/envmod')+':'+str(ROOT/'build')+':'+str(simlib)],check=True)

def observations(run):
    result={}; counts=defaultdict(int)
    for path in (run/'results').glob('*.sca'):
        for line in path.open():
            if line.startswith('scalar '):
                _,module,name,value=shlex.split(line)
                if name.startswith('cpmLinkPrr.') or name=='testGlobalRngDraws':
                    value=float(value); result[module,name]=None if math.isnan(value) else value
    for path in (run/'results').glob('*.vec'):
        ids={}
        for line in path.open():
            if line.startswith('vector '):
                _,number,module,name,*_=shlex.split(line)
                if name in ['transmission:vector','cpmPacketSize:vector','cpmTxObjectCount:vector']:
                    ids[number]=name
            elif line and line[0].isdigit():
                fields=line.split(); name=ids.get(fields[0])
                if name: counts[name]+=1 if name=='transmission:vector' else int(float(fields[-1]))
    result.update(counts)
    return result

samples={False:[],True:[]}; reference=None
for i in range(args.repeats):
    # Alternate order to reduce systematic warm-cache bias.
    for disabled in ([False,True] if i%2==0 else [True,False]):
        command=['python3',str(Path(__file__).with_name('integration.py')),'--output',str(args.output),
            '--method',args.method,'--until',args.until,'--repeat',str(i),'--benchmark-probe',str(library)]
        if disabled: command.append('--disabled')
        subprocess.run(command,cwd=ROOT,check=True)
        label='road120-'+args.method+('-disabled' if disabled else '')+'-'+str(i)
        run=args.output/label
        current=observations(run)
        assert any(isinstance(k,tuple) and k[1]=='testGlobalRngDraws' for k in current),'missing RNG probe'
        if reference is None: reference=current
        assert current==reference,(label,'observer altered aggregate RNG, application submissions/bytes/objects, or link PRR')
        seconds,kib=(run/'resource.txt').read_text().split(); samples[disabled].append((float(seconds),int(kib)))
enabled=statistics.median(x[0] for x in samples[False]); disabled=statistics.median(x[0] for x in samples[True])
summary={'method':args.method,'repeats':args.repeats,'until':args.until,'enabledSecondsMedian':enabled,
    'disabledSecondsMedian':disabled,'wallTimeChangePercent':100*(enabled/disabled-1),
    'enabledPeakRssKiBMedian':statistics.median(x[1] for x in samples[False]),
    'disabledPeakRssKiBMedian':statistics.median(x[1] for x in samples[True]),
    'globalRngDraws':next(v for k,v in reference.items() if isinstance(k,tuple) and k[1]=='testGlobalRngDraws'),
    'sameApplicationAndPrrResults':True,'samples':{str(k):v for k,v in samples.items()}}
(args.output/'benchmark.json').write_text(json.dumps(summary,indent=2)+'\n'); print(json.dumps(summary),flush=True)
