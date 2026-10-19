#!/usr/bin/env python3
"""Check independent payload traces, recorded vectors, and byte/time conservation."""
from pathlib import Path
from collections import defaultdict
import argparse
import json
import math
import shlex

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('runs',nargs='+',type=Path)
args=parser.parse_args()
summary={}
for run in args.runs:
    log=(run/'simulation.log').read_text()
    assert 'Simulation time limit reached' in log and '<!> Error:' not in log,run
    by_module=defaultdict(lambda:defaultdict(list))
    for line in (run/'trace.jsonl').open():
        row=json.loads(line)
        by_module[row['module']][row['event']].append(row)
    vectors=defaultdict(list)
    for file in (run/'results').glob('*.vec'):
        ids={}
        for line in file.open():
            if line.startswith('vector '):
                _,number,module,name,*_=shlex.split(line)
                ids[number]=(module,name)
            elif line and line[0].isdigit():
                fields=line.split()
                if len(fields)==4 and fields[0] in ids:
                    vectors[ids[fields[0]]].append((float(fields[2]),float(fields[3])))
    scalars={}
    for file in (run/'results').glob('*.sca'):
        for line in file.open():
            if line.startswith('scalar '):
                _,module,name,value=shlex.split(line)
                scalars[module,name]=float(value)
    totals=defaultdict(int)
    for module,events in by_module.items():
        start=events['start'][0]
        assert len(events['start'])==len(events['finish'])==1,(module,'lifecycle')
        end=events['finish'][0]['t']
        begin=max(start['t'],start['warmup'])
        requests=events['request']
        sizes=events['cpmPacketSize']; objects=events['CpmTxObjectCount']; submissions=events['CpmSubmitted']
        assert len(requests)==len(sizes)==len(objects)==len(submissions),(module,'missing/duplicate accounting')
        for req,size,obj,submitted in zip(requests,sizes,objects,submissions):
            assert req['t']==size['t']==obj['t']==submitted['t']
            assert req['bytes']==size['value']>0,(module,'encoded bytes')
            assert req['objects']==obj['value']<=255,(module,'encoded objects')
            assert submitted['value']==1
            if run.name!='fixture': assert req.get('wire_decoded',True),(module,'wire decode')
        measured=[r for r in requests if r['t']>=begin]
        for event,stat in [('CpmSubmitted','transmission'),('cpmPacketSize','cpmPacketSize'),('CpmTxObjectCount','cpmTxObjectCount')]:
            recorded=vectors[(module,stat+':vector')]
            wanted=[r for r in events[event] if r['t']>=begin]
            assert len(recorded)==len(wanted),(module,stat,len(recorded),len(wanted))
            for index,((t,value),row) in enumerate(zip(recorded,wanted),1):
                assert math.isclose(t,row['t'],abs_tol=1e-10)
                assert value==(index if event=='CpmSubmitted' else row['value']),(module,stat,'recorded sample')
        rates=events['CpmTxThroughput']; durations=events['CpmTxWindowDuration']; byte_windows=events['CpmTxWindowBytes']
        assert len(rates)==len(durations)==len(byte_windows)
        cursor=0; previous=begin
        for rate,duration,window in zip(rates,durations,byte_windows):
            t=rate['t']
            assert duration['t']==window['t']==t
            expected_duration=t-previous
            assert 0<duration['value']<=1.00000001
            assert math.isclose(duration['value'],expected_duration,abs_tol=1e-9),(module,'duration')
            if t!=end: assert math.isclose(t,round(t),abs_tol=1e-10),(module,'unaligned window')
            bytes_expected=0
            while cursor<len(measured) and measured[cursor]['t']<=t+1e-10:
                bytes_expected+=measured[cursor]['bytes']; cursor+=1
            assert window['value']==bytes_expected,(module,t,window['value'],bytes_expected)
            assert math.isclose(rate['value'],8*bytes_expected/duration['value'],rel_tol=1e-12,abs_tol=1e-9)
            totals['zero_windows']+=rate['value']==0
            totals['windows']+=1
            previous=t
        assert cursor==len(measured),(module,'lost final bytes')
        active=max(0,end-begin)
        assert math.isclose(sum(r['value'] for r in durations),active,abs_tol=1e-9),(module,'lost active time')
        byte_total=sum(r['bytes'] for r in measured)
        assert sum(r['value'] for r in byte_windows)==byte_total,(module,'byte conservation')
        for event,stat in [('CpmTxThroughput','cpmTxThroughput'),('CpmTxWindowBytes','cpmTxWindowBytes'),('CpmTxWindowDuration','cpmTxWindowDuration')]:
            recorded=vectors[(module,stat+':vector')]
            wanted=events[event]
            assert len(recorded)==len(wanted),(module,stat,'recorded windows')
            for (t,value),row in zip(recorded,wanted):
                assert math.isclose(t,row['t'],abs_tol=1e-10)
                assert math.isclose(value,row['value'],rel_tol=1e-12,abs_tol=1e-9)
        if scalars:
            assert scalars[module,'cpmApplicationSubmissions']==len(measured)
            assert scalars[module,'cpmTxPayloadBytes']==byte_total
            assert math.isclose(scalars[module,'cpmTxActiveDuration'],active,abs_tol=1e-9)
            if active:
                assert math.isclose(scalars[module,'cpmTxThroughput:mean'],byte_total*8/active,rel_tol=1e-12,abs_tol=1e-9)
        if events['decision']:
            prior=None
            requests_at={r['t']:r for r in requests}
            for row in events['decision']:
                assert row['history']==0
                if prior is not None: assert math.isclose(row['t']-prior,0.1,abs_tol=1e-9)
                prior=row['t']
                assert row['sent']==(row['t'] in requests_at),(module,'suppression')
                if row['sent']: assert set(row['ids'][:255])==set(requests_at[row['t']]['ids']),(module,'all selected objects')
                else:
                    totals['suppressed_decisions']+=1
                    if 'local_objects' in row:
                        totals['suppressed_no_objects' if row['local_objects']==0 else 'suppressed_redundant']+=1
                if row['sent'] and 'local_objects' in row:
                    assert len(row['ids'])==row['local_objects'],(module,'all local inclusion')
        totals['submissions']+=len(measured)
        totals['forwarded_submissions']+=sum(r['forwarded'] for r in measured)
        totals['bytes']+=byte_total
        totals['stations']+=1
    if run.name=='fixture':
        assert not by_module['CpmAccountingNetwork.idle']['request']
        cap=[r for r in by_module['CpmAccountingNetwork.active']['request'] if r['t']==2.2]
        assert len(cap)==1 and cap[0]['objects']==255
        no_objects=[r for r in by_module['CpmAccountingNetwork.active']['request'] if r['t']==2.8]
        assert len(no_objects)==1 and no_objects[0]['objects']==0 and no_objects[0]['bytes']>0
        assert by_module['CpmAccountingNetwork.active']['finish'][0]['t']==3.25
        assert by_module['CpmAccountingNetwork.late']['finish'][0]['t']==2.75
    else:
        if run.name.endswith('Proposed'):
            assert totals['suppressed_no_objects']>0 and totals['suppressed_redundant']>0
        rsus={module.split('.middleware')[0] for module in by_module if module.startswith('World.rsu')}
        assert rsus=={'World.rsu'+str(i) for i in range(7)},rsus
        if run.name.endswith(('VPCast','wPPersistence')): assert totals['forwarded_submissions']>0
    summary[run.name]=dict(totals)
    print('PASS',run.name,dict(totals))
(args.runs[0].parent/'validation-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
