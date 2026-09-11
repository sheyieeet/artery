import sys
import xml.etree.ElementTree as ET

tree = ET.parse('gushan.net.xml')
root = tree.getroot()
total_edge_length = 0.0
total_lane_length = 0.0

for edge in root.findall('edge'):
    if not edge.get('function') == 'internal':
        # Assuming all lanes in an edge have roughly the same length, or we just take the first lane's length for edge length
        lanes = edge.findall('lane')
        if lanes:
            edge_length = float(lanes[0].get('length', 0))
            total_edge_length += edge_length
            for lane in lanes:
                total_lane_length += float(lane.get('length', 0))

print(f"Total edge length: {total_edge_length/1000} km")
print(f"Total lane length: {total_lane_length/1000} km")
