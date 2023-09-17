"""
Update some test functions of the metis library to work with newer versions of networkx.
Tested with python 3.11 and networkx 3.1
"""

import networkx as nx
import metis

def example_networkx():
    G = nx.Graph()
    nx.add_star(G, [0,1,2,3,4])
    nx.add_path(G, [4,5,6,7,8])
    nx.add_star(G, [8,9,10,11,12])
    nx.add_path(G, [6,13,14,15])
    nx.add_star(G, [15,16,17,18])
    return G
