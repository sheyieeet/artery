import sys
import xml.etree.ElementTree as ET

tree = ET.parse('grid_5x5/grid_scenario_traffic_light.net.xml')
root = tree.getroot()
total_lane_length = 0.0

for edge in root.findall('edge'):
    if not edge.get('function') == 'internal':
        for lane in edge.findall('lane'):
            length = float(lane.get('length', 0))
            total_lane_length += length

print(f"Total lane length: {total_lane_length/1000} km")
