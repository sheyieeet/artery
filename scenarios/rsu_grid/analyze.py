import csv
from collections import defaultdict

with open('cpm_metrics.csv', 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read().replace('\x00', '')
    reader = csv.DictReader(content.splitlines())
    rows = list(reader)

print("=== Simulation Analysis ===")
print(f"Total Rows: {len(rows)}")

station_types = set()
rsu_ids = set()
veh_ids = set()

for row in rows:
    stype = row['StationType']
    sid = row['StationID']
    if stype == 'RSU':
        rsu_ids.add(sid)
    elif stype == 'Vehicle':
        veh_ids.add(sid)

print("\n=== Station Counts ===")
print(f"RSU: {len(rsu_ids)}")
print(f"Vehicle: {len(veh_ids)}")

metrics_data = defaultdict(lambda: defaultdict(list))
for row in rows:
    metric = row['Metric']
    stype = row['StationType']
    val = float(row['Value'])
    metrics_data[metric][stype].append(val)

for metric, types_data in metrics_data.items():
    print(f"\n=== Metric: {metric} ===")
    all_vals = []
    for vals in types_data.values():
        all_vals.extend(vals)
    
    if not all_vals: continue
    
    print(f"Total entries: {len(all_vals)}")
    print(f"Overall Min: {min(all_vals)}")
    print(f"Overall Max: {max(all_vals)}")
    print(f"Overall Mean: {sum(all_vals)/len(all_vals)}")
    
    print("\nBreakdown by StationType:")
    for stype, vals in types_data.items():
        if vals:
            print(f"  {stype}: count={len(vals)}, min={min(vals)}, max={max(vals)}, mean={sum(vals)/len(vals)}")

    
