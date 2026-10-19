#!/usr/bin/env python3
"""Stream freshness outputs, reconcile exposure, and check complete metadata coverage."""
from pathlib import Path
from collections import defaultdict
import argparse
import json
import math
import shlex

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('runs',nargs='+',type=Path)
args=parser.parse_args()
for run in args.runs:
    log=(run/'simulation.log').read_text()
    assert '<!> Error:' not in log and 'Simulation time limit reached' in log,run
    scalars={}
    for path in (run/'results').glob('*.sca'):
        for line in path.open():
            if line.startswith('scalar '):
                _,module,name,value=shlex.split(line)
                if name.startswith('objectFreshness.'): scalars[name]=float(value)
    assert scalars, (run,'missing freshness scalars')
    counts=defaultdict(int); sums=defaultdict(float)
    for path in (run/'results').glob('*.vec'):
        ids={}
        for line in path.open():
            if line.startswith('vector '):
                _,number,module,name,*_=shlex.split(line)
                if module.endswith('.cpmObjectFreshness') and 'PhysicalObject' in name:
                    ids[number]=name.removesuffix(':vector')
            elif line and line[0].isdigit():
                fields=line.split(); name=ids.get(fields[0])
                if name:
                    value=float(fields[-1]); assert math.isfinite(value) and value>=0,(run,name,value)
                    if 'FreshUpdateInterval' in name: assert value>0,(run,'zero fresh interval')
                    counts[name]+=1; sums[name]+=value
    summary={}
    for mode,prefix in [('cpmOnly','cpmPhysicalObject'),('combined','combinedPhysicalObject')]:
        for role,suffix in [('vehicle','Vehicle'),('rsu','Rsu')]:
            key='objectFreshness.'+mode+'.'+role+'.'
            s={name[len(key):]:value for name,value in scalars.items() if name.startswith(key)}
            assert s, (run,key)
            assert s['freshUpdates']>0, (run,key,'integration missed receiver population')
            assert s['freshUpdates']==s['completedInitialWaits']+s['completedIntervals'],(run,key,'update conservation')
            assert s['eligibleEpisodes']==s['completedInitialWaits']+s['censoredInitialWaits'],(run,key,'episode conservation')
            assert s['zeroUpdateEpisodes']==s['censoredInitialWaits'],(run,key,'zero-update conservation')
            assert s['eligibleEpisodes']==sum(s[r+'Episodes'] for r in ['rangeExit','objectDeparture','receiverDeparture','termination'])
            assert math.isclose(s['eligibleSeconds'],s['completedIntervalSeconds']+s['completedInitialWaitSeconds']+s['censoredSeconds'],rel_tol=1e-10,abs_tol=1e-8),(run,key,'unaccounted observation gap')
            for kind,stat in [('FreshUpdateInterval','completedIntervals'),('InitialWait','completedInitialWaits'),
                              ('CensoredInitialWait','censoredInitialWaits'),('CensoredGap','censoredGaps')]:
                assert counts[prefix+kind+suffix]==s[stat],(run,key,kind,'vector/scalar disagreement')
            summary[mode+'.'+role]={k:s[k] for k in ['eligibleEpisodes','zeroUpdateEpisodes','freshUpdates','completedIntervals','eligibleSeconds','censoredSeconds']}
    for field in ['missingMetadataEntries','unresolvedProvenanceEntries','invalidInformationTimes']:
        assert scalars['objectFreshness.'+field]==0,(run,field,scalars['objectFreshness.'+field])
    assert scalars['objectFreshness.rsuReceivers']==7,(run,'Road 120 RSU registry')
    summary['state']={k.removeprefix('objectFreshness.'):v for k,v in scalars.items() if k.startswith('objectFreshness.peak')}
    print(json.dumps({'run':str(run),**summary}),flush=True)
