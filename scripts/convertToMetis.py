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
from pymetis import Options, part_graph

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

    nodesDict = {}
    numNodes = len(nodes)

    # for every node i, list of its neighbors indices
    neighbors = [None] * numNodes
    neighbor_edge_wgts = [None] * numNodes
    num_neighs_total = 0
    for i, node in enumerate(nodes):
        nodesDict[node] = i
    for i, node in enumerate(nodes):
        outgoing_edges: list[sumolib.net.edge.Edge] = node.getOutgoing()
        neighbor_edge_wgts[i] = [_get_edge_weight(edge) for edge in outgoing_edges]
        # Do not use getNeighboringNodes to make sure weights are in same order as edges
        neighs = [_get_other_node(edge, node) for edge in outgoing_edges]
        neighbors[i] = [nodesDict[nnode] for nnode in neighs]
        num_neighs_total += len(neighbors[i])

    # print(f"neighbor lists: {neighbors}")

    xadj, adjncy, eweights = _neighbors_to_xadj(neighbors, num_neighs_total, neighbor_edge_wgts)

    # execute metis
    # edgecuts: amount of edges lying between partitions, that were cut
    metis_opts = Options()
    metis_opts.contig = True
    edgecuts, parts = part_graph(
        numparts, xadj=xadj, adjncy=adjncy,
        # eweights=eweights,
        # options=metis_opts,
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

def _get_other_node(edge, node):
    to_node: sumolib.net.node.Node = edge.getToNode()
    from_node: sumolib.net.node.Node = edge.getToNode()
    return to_node if to_node.getID() != node.getID() else from_node

DEFAULT_WGT = 10

def _get_edge_weight(edge: sumolib.net.edge.Edge):
    # later could add more ways of determining weight and/or options
    return _get_edge_weight_osm(edge)

OSM_HIGHWAY_WEIGHTS = {
    'motorway': 150,
    'trunk': 150,
    'primary': 100,
    'secondary': 50,
    'tertiary': 15,
    'unclassified': 15,
    'residential': 10,
}

def _get_edge_weight_osm(edge: sumolib.net.edge.Edge):
    edge_type = edge.getType()
    split = edge_type.split('.')
    if len(split) > 1 and split[0] == 'highway' and split[1] in OSM_HIGHWAY_WEIGHTS:
        return OSM_HIGHWAY_WEIGHTS[split[1]]
    return DEFAULT_WGT

# convert from neighbor list to C-like metis format
def _neighbors_to_xadj(neighbors: list, num_edges: int, *other_lists: list) -> tuple[list, list]:
    # The adjacency structure of the graph is stored as follows: The
    # adjacency list of vertex *i* is stored in array *adjncy* starting at
    # index ``xadj[i]`` and ending at (but not including) index ``xadj[i +
    # 1]``. That is, for each vertex i, its adjacency list is stored in
    # consecutive locations in the array *adjncy*, and the array *xadj* is
    # used to point to where it begins and where it ends.

    xadj = [None] * (len(neighbors) + 1)
    adjncy = [None] * num_edges
    other_lists_out = [[None] * num_edges] * len(other_lists)

    adj_idx = 0
    for id, neigh_elements in enumerate(zip(neighbors, *other_lists)):
        xadj[id] = adj_idx
        neighs = neigh_elements[0]
        for i, neigh in enumerate(neighs):
            adjncy[adj_idx] = neigh
            if len(neigh_elements) > 1:
                for j, neigh_list in enumerate(neigh_elements[1:]):
                    other_lists_out[j][adj_idx] = neigh_list[i]
            adj_idx += 1
        xadj[id+1] = adj_idx # mostly redundant, but covers last one

    return xadj, adjncy, *other_lists_out


if __name__ == "__main__":
    args = parser.parse_args()
    main(args.netfile, args.numparts)