# convertToMetis.py
# Author: Phillip Taylor
# Tweaked by Filippo Lenzi

"""
Convert SUMO network into proper format for METIS input, partition with METIS,
and write in one file per partition the SUMO network edges of that partition.

"""
import os
import sys

import argparse

parser = argparse.ArgumentParser()
parser.add_argument('netfile', help="SUMO network file to partition (in .net.xml format)")
parser.add_argument('numparts', type=int, help="Amount of partitions to create. Might end up being lower in the output in small graphs.")

if 'SUMO_HOME' in os.environ:
    tools = os.path.join(os.environ['SUMO_HOME'], 'tools')
    sys.path.append(os.path.join(tools))
    from sumolib.output import parse, parse_fast
    from sumolib.net import readNet
    import sumolib
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

if 'METIS_DLL' in os.environ:
    import metis
else:
    sys.exit("please declare environment variable 'METIS_DLL' to the complete path of the metis library dll or lib file! (See README)")

def remove_non_empty_parts(partitions: list[int]):
    """From a list assigning a partition to each index, remove 
    missing indices (meaning, shift later ones back so they are a
    continuous range).
    METIS might sometimes return empty partitions in small graphs.
    """

    part_values = set(partitions)
    min_val = 0
    max_val = max(part_values)
    missing_values = set(range(min_val, max_val + 1)) - part_values
    offsets = {v: len([x for x in missing_values if x < v]) for v in part_values}

    return [x - offsets[x] for x in partitions]

def main(netfile: str, numparts: int):
    net = readNet(netfile)
    nodes = net.getNodes()
    nodesDict = {}
    numNodes = len(nodes)
    # for every node i, list of its neighbors indices
    neighbors = [None] * numNodes
    numUndirectedEdges = 0
    for i, node in enumerate(nodes):
        nodesDict[node] = i
    for i, node in enumerate(nodes):
        neighs = node.getNeighboringNodes()
        # print(f"{i}/{node}: {neighs}")
        for n in neighs:
            if n not in nodesDict:
                numUndirectedEdges+=1
        neighbors[i] = [nodesDict[nnode] for nnode in neighs]

    # print(f"neighbor lists: {neighbors}")

    # execute metis
    # edgecuts: amount of edges lying between partitions, that were cut
    edgecuts, parts = metis.part_graph(
        neighbors, nparts=numparts, 
        objtype='vol',
        contig=True,
    )

    # parts is a list like this: parts[nodeid] = partid
    # might have empty partitions with metis in small graphs, so remove em
    parts = remove_non_empty_parts(parts)

    print("parts:", parts)

    actual_numparts = len(set(parts))

    # get edges corresponding to partitions
    edges = [set() for _ in range(actual_numparts)]
    for node, partition in zip(nodes, parts):
        nodeEdges = node.getIncoming() + node.getOutgoing()
        # print(f"{node} in {partition}: {nodeEdges}")
        for e in nodeEdges:
            if e.getID() not in edges[partition]:
                edges[partition].add(e.getID())

    print("edges", edges)

    with open(os.path.join("data", "numParts.txt"), 'w', encoding='utf-8') as f:
        f.write(f"{actual_numparts}")

    # write edges of partitions in separate files
    for i, edge_set in enumerate(edges):
        with open(os.path.join("data", f"edgesPart{i}.txt"), 'w', encoding='utf-8') as f:
            for eID in edge_set:
                print(eID, file=f)

if __name__ == "__main__":
    args = parser.parse_args()
    main(args.netfile, args.numparts)
