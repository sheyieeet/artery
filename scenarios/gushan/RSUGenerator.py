#!/usr/bin/env python
import argparse
import math
import os
import sys
import xml.etree.ElementTree as etree

if 'SUMO_HOME' in os.environ:
    tools = os.path.join(os.environ['SUMO_HOME'], 'tools')
    sys.path.append(tools)
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

import sumolib  # noqa


parser = argparse.ArgumentParser(
    description='RSU XML generation script for junctions.',
    epilog="""This script reads junctions coordinates and their edges from
        SUMO network XML file. Generates a RSU in the center of every junction
        and adds an antenna for each outgoing edge.""")

parser.add_argument(
    '-n', help='SUMO network file to parse',
    type=str, metavar='<path>/<scenario>.net.xml', required=True)

parser.add_argument(
    '-o', help='File to write RSU definitions to',
    type=str, metavar='<path>/RSU.xml', required=True)


args = parser.parse_args()
net = sumolib.net.readNet(args.n)
netBBox = net.getBBoxXY()
junctionCount = 0

root = etree.Element("RSUs")
root.append(etree.Comment(
    "This file was generated with '{}'".format(" ".join(sys.argv))))

for junction in net.getNodes():
    # Only place RSUs at traffic light junctions to avoid deploying them everywhere
    if junction.getType() != "traffic_light":
        continue

    junctionCount += 1

    nodePos = junction.getCoord()
    oppPosX = nodePos[0] - netBBox[0][0]
    oppPosY = netBBox[1][1] - nodePos[1]

    doc = etree.SubElement(
        root, "rsu",
        id=junction.getID(),
        positionX=str(oppPosX), positionY=str(oppPosY))

    for edge in junction.getOutgoing():
        outgoingNodePos = edge.getToNode().getCoord()
        dx = outgoingNodePos[0] - nodePos[0]
        dy = outgoingNodePos[1] - nodePos[1]
        length = math.sqrt(dx**2 + dy**2)
        if length == 0:
            continue
        nx = dx / length
        ny = dy / length
        ny = ny * -1
        direction = math.atan2(ny, nx)

        if(direction == math.pi * -1):
            direction += 2 * math.pi

        if(direction == -0):
            direction = 0

        antenna = etree.SubElement(doc, "antenna", direction=str(direction))

tree = etree.ElementTree(root)
tree.write(args.o, encoding="utf-8", xml_declaration=True)
print(f"Generated {junctionCount} RSUs at traffic light intersections.")
