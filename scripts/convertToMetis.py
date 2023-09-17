# convertToMetis.py
# Author: Phillip Taylor
# Tweaked by Filippo Lenzi

"""
Convert SUMO network into proper format for METIS input, partition with METIS,
and write in one file per partition the SUMO network edges of that partition.

"""
from __future__ import absolute_import
from __future__ import print_function

import os
import sys
import codecs
import copy
import subprocess
import metis

from optparse    import OptionParser
from collections import defaultdict

if 'SUMO_HOME' in os.environ:
    tools = os.path.join(os.environ['SUMO_HOME'], 'tools')
    sys.path.append(os.path.join(tools))
    from sumolib.output import parse, parse_fast
    from sumolib.net import readNet
    import sumolib
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")


def get_options(args=sys.argv[1:]):
    optParser = OptionParser()
    options, args = optParser.parse_args(args=args)
    options.network = args[0]
    options.parts = args[1]
    return options


def main(options):
    net = readNet(options.network)
    nodes = net.getNodes()
    nodesDict = {}
    numNodes = len(nodes)
    # for every node i, list of its neighbors indices
    neighbors = [None] * numNodes
    numUndirectedEdges = 0
    for i in range(numNodes):
        nodesDict[nodes[i]] = i
        neighs = nodes[i].getNeighboringNodes()
        for n in neighs:
            if n not in nodesDict:
                numUndirectedEdges+=1
        neighbors[i] = neighs

    # execute metis
    # params passed by original program not represented here: 
    # "-objtype=vol", "-contig"
    objval, parts = metis.part_graph(
        neighbors, nparts=options.parts, 
        objtype='vol',
        contig=True,
    )

    # get edges corresponding to partitions
    edges = [set() for _ in range(int(options.parts))]
    curr = 0
    with codecs.open("metisInputFile.metis.part."+options.parts, 'r', encoding='utf8') as f:
        for line in f:
            part = int(line)
            nodeEdges = nodes[curr].getIncoming() + nodes[curr].getOutgoing()
            for e in nodeEdges:
                if e.getID() not in edges[part]:
                    edges[part].add(e.getID())
            curr+=1

    # write edges of partitions in separate files
    for i in range(len(edges)):
        with codecs.open("edgesPart"+str(i)+".txt", 'w', encoding='utf8') as f:
            for eID in edges[i]:
                f.write("%s\n" % (eID))



if __name__ == "__main__":
    main(get_options())
