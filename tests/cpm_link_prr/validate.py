#!/usr/bin/env python3
"""Validate aggregate PRR and controlled-radio outcomes without packet logs."""
from pathlib import Path
from collections import defaultdict
import argparse
import json
import math
import shlex

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('runs',nargs='+',type=Path)
args=parser.parse_args()

def read_scalars(run):
    values={}
    for path in (run/'results').glob('*.sca'):
        for line in path.open():
            if line.startswith('scalar '):
                _,module,name,value=shlex.split(line)
                values[module,name]=float(value)
    return values

fixture_counts={ # air, eligible, received, failed, censored departure, censored end
    'lossfree':(4,12,12,0,0,0), 'loss':(1,3,2,1,0,0),
    'corrupt':(1,3,2,1,0,0), 'repeated':(2,6,6,0,0,0),
    'duplicate':(1,3,3,0,0,0), 'departure':(1,3,2,0,1,0),
    'senderDeparture':(1,3,0,0,3,0), 'termination':(1,3,0,0,0,3),
    'entry':(1,2,2,0,0,0), 'eligibility':(1,1,1,0,0,0),
    'range':(1,5,5,0,0,0), 'warmup':(1,3,3,0,0,0),
    'rng':(2,6,6,0,0,0), 'stress':(1000,3000,3000,0,0,0),
    'noReceivers':(1,0,0,0,0,0),
}
fields=['eligible','received','failed','censoredDeparture','censoredTermination']
summary={}
for run in args.runs:
    log=(run/'simulation.log').read_text()
    assert '<!> Error:' not in log and ('Simulation time limit reached' in log or 'Simulation stopped with endSimulation()' in log),run
    values=read_scalars(run)
    prr={name:value for (module,name),value in values.items() if module.endswith('.cpmLinkPrr')}
    assert prr, (run,'missing PRR scalars')
    prefixes={key.rsplit('.',1)[0] for key in prr if key.endswith('.eligible')}
    for prefix in prefixes:
        counts={f:prr[prefix+'.'+f] for f in fields}
        assert counts['eligible']==sum(counts[f] for f in fields[1:]),(run,prefix,'conservation')
        resolved=counts['received']+counts['failed']
        assert prr[prefix+'.resolved']==resolved
        ratio=prr[prefix+'.ratio']
        if resolved:
            assert 0<=ratio<=1 and math.isclose(ratio,counts['received']/resolved,rel_tol=1e-12)
        else: assert math.isnan(ratio)
    for f in fields:
        pooled=prr['cpmLinkPrr.all.total.'+f]
        assert pooled==sum(prr['cpmLinkPrr.'+role+'.total.'+f] for role in ['VV','VR','RV','RR'])
        assert pooled==sum(prr[f'cpmLinkPrr.all.distance{b}to{b+25}m.'+f] for b in range(0,500,25))
    total={f:prr['cpmLinkPrr.all.total.'+f] for f in fields}
    total['air']=prr['cpmLinkPrr.airTransmissions']
    total['ratio']=prr['cpmLinkPrr.all.total.ratio']
    total['peak_flights']=prr['cpmLinkPrr.peakActiveTransmissions']
    total['peak_pairs']=prr['cpmLinkPrr.peakActiveOpportunities']
    total['peak_associations']=prr['cpmLinkPrr.peakFrameAssociations']
    if run.name.startswith('fixture-'):
        case=run.name.removeprefix('fixture-')
        assert tuple(total[f] for f in ['air',*fields])==fixture_counts[case],(run,total)
        independent={name:value for (module,name),value in values.items() if module.endswith('.control')}
        assert independent['independentAirFrames']==independent['independentUniquePhysicalIds']
        if case!='warmup': assert total['air']==independent['independentAirFrames']
        if case not in ['warmup','eligibility','range','entry']:
            assert total['received']==independent['independentMacAccepts']
        if case=='duplicate': assert prr['cpmLinkPrr.duplicateMacCallbacks']==3
        if case=='range':
            for b,n in [(0,2),(25,1),(475,2)]:
                assert prr[f'cpmLinkPrr.all.distance{b}to{b+25}m.eligible']==n
        if case=='lossfree':
            for role,n in [('VV',2),('VR',4),('RV',4),('RR',2)]:
                assert prr[f'cpmLinkPrr.{role}.total.received']==n
            for b,n in [(25,6),(50,4),(75,2)]:
                assert prr[f'cpmLinkPrr.all.distance{b}to{b+25}m.eligible']==n
        if case=='stress':
            assert total['peak_flights']==1 and total['peak_pairs']==3
        if case=='rng':
            disabled=read_scalars(run.with_name(run.name+'-observer-disabled'))
            reference={name:value for (module,name),value in disabled.items() if module.endswith('.control')}
            assert independent==reference,(run,'observer changed RNG draws or radio results',independent,reference)
            total['rng_draws']=independent['globalRngDraws']
    else:
        vector_ids={}
        submissions=0
        station_modules=set()
        for path in (run/'results').glob('*.vec'):
            vector_ids.clear()
            for line in path.open():
                if line.startswith('vector '):
                    _,number,module,name,*_=shlex.split(line)
                    if name=='cpmPacketSize:vector': vector_ids[number]=module
                    if name=='cpmPacketSize:vector': station_modules.add(module)
                elif line and line[0].isdigit():
                    parts=line.split()
                    if parts[0] in vector_ids: submissions+=1
        total['application_submissions']=submissions
        assert total['air']<=submissions,(run,'air count exceeds app submissions')
        rsus={module.split('.')[1] for module in station_modules if '.rsu' in module}
        assert len(rsus)==7,(run,'RSU count',rsus)
        for role in ['VV','VR','RV']:
            assert prr[f'cpmLinkPrr.{role}.total.eligible']>0,(run,'missing vehicle/RSU role',role)
        dcc=defaultdict(float)
        for (_,name),value in values.items():
            if name.startswith('testDcc'): dcc[name]+=value
        if dcc:
            total['dcc']=dict(dcc)
            assert dcc['testDccDropEvents']>0
            assert dcc['testDccCpmReleases']>=total['air']
            assert submissions>5*total['air']
    if math.isnan(total['ratio']): total['ratio']=None
    summary[run.name]=total
print(json.dumps(summary,indent=2,allow_nan=False))
