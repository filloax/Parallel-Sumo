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
from pymetisdoc import part_graph

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

def remove_empty_parts(partitions: list[int], warn: bool = True):
    """From a list assigning a partition to each index, remove 
    missing indices (meaning, shift later ones back so they are a
    continuous range).
    METIS might sometimes return empty partitions in small graphs.
    """

    part_values = set(partitions)
    min_val = 0
    max_val = max(part_values)
    missing_values = set(range(min_val, max_val + 1)) - part_values
    if warn and len(missing_values) > 0:
        print(f"[WARN] Empty partitions: <{', '.join([str(x) for x in missing_values])}>", file=sys.stderr)
    offsets = {v: len([x for x in missing_values if x < v]) for v in part_values}

    return [x - offsets[x] for x in partitions]

def main(netfile: str, numparts: int):
    net: sumolib.net.Net = readNet(netfile)
    nodes: list[sumolib.net.node.Node] = net.getNodes()
    edges: list[sumolib.net.edge.Edge] = net.getEdges()

    nodesDict = {}
    numNodes = len(nodes)

    # for every node i, list of its neighbors indices
    neighbors = [None] * numNodes
    num_neighs_total = 0
    for i, node in enumerate(nodes):
        nodesDict[node] = i
    for i, node in enumerate(nodes):
        neighs = node.getNeighboringNodes()
        neighbors[i] = [nodesDict[nnode] for nnode in neighs]
        num_neighs_total += len(neighbors[i])

    # print(f"neighbor lists: {neighbors}")

    xadj, adjncy = _neighbors_to_xadj(neighbors, num_neighs_total)

    # execute metis
    # edgecuts: amount of edges lying between partitions, that were cut
    edgecuts, parts = part_graph(
        numparts, xadj=xadj, adjncy=adjncy,
        contiguous=True,
    )

    # parts is a list like this: parts[nodeid] = partid
    # might have empty partitions with metis in small graphs, so remove them
    parts = remove_empty_parts(parts)

    # print("parts:", parts)

    actual_numparts = len(set(parts))

    # get edges corresponding to partitions
    edges = [set() for _ in range(actual_numparts)]
    for node, partition in zip(nodes, parts):
        nodeEdges = node.getIncoming() + node.getOutgoing()
        # print(f"{node} in {partition}: {nodeEdges}")
        for e in nodeEdges:
            if e.getID() not in edges[partition]:
                edges[partition].add(e.getID())

    # print("edges", edges)

    with open(os.path.join("data", "numParts.txt"), 'w', encoding='utf-8') as f:
        f.write(f"{actual_numparts}")

    # write edges of partitions in separate files
    for i, edge_set in enumerate(edges):
        with open(os.path.join("data", f"edgesPart{i}.txt"), 'w', encoding='utf-8') as f:
            for eID in edge_set:
                print(eID, file=f)

# convert from neighbor list to C-like metis format
def _neighbors_to_xadj(neighbors: list, num_edges: int) -> tuple[list, list]:
    # The adjacency structure of the graph is stored as follows: The
    # adjacency list of vertex *i* is stored in array *adjncy* starting at
    # index ``xadj[i]`` and ending at (but not including) index ``xadj[i +
    # 1]``. That is, for each vertex i, its adjacency list is stored in
    # consecutive locations in the array *adjncy*, and the array *xadj* is
    # used to point to where it begins and where it ends.

    xadj = [None] * (len(neighbors) + 1)
    adjncy = [None] * num_edges

    adj_idx = 0
    for id, neighs in enumerate(neighbors):
        xadj[id] = adj_idx
        for neigh in neighs:
            adjncy[adj_idx] = neigh
            adj_idx += 1
        xadj[id+1] = adj_idx # mostly redundant, but covers last one

    return xadj, adjncy


if __name__ == "__main__":
    args = parser.parse_args()
    main(args.netfile, args.numparts)