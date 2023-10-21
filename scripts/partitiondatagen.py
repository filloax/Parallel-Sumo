"""
processNeighbors.py

Find partition neighbors and process shared edge data, to be
used in ParallelSumo. Should be used after createParts.py
generated the required files.
"""

from xml.etree import ElementTree as ET
from collections import defaultdict
import os, sys
import json

class PartitionDataGen:
    num_parts: int
    netfiles: dict[int, ET.ElementTree]
    edge_parts: dict[str, tuple[int, int]]
    data_folder: str
    
    def __init__(self, num_parts: int, data_folder: str = "data"):
        self.num_parts = num_parts
        self.netfiles = {}
        self.edge_parts = defaultdict(tuple)
        self.data_folder = data_folder

    def __load_edges(self):
        self.edge_parts = defaultdict(tuple)
        all_edges_l = defaultdict(list)
        for part_idx in range(self.num_parts):
            net_path = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.net.xml"))
            net_file = ET.parse(net_path)
            self.netfiles[part_idx] = net_file
            netEl = net_file.getroot()
            for el in netEl.findall("edge"):
                if el.get("function", None) is None or el.get("function") != "internal":
                    all_edges_l[el.get("id")].append(part_idx)
        for id in all_edges_l:
            parts = all_edges_l[id]
            if len(parts) > 2:
                print(f"[WARN] Edge {id} is in more than two partitions: {parts}", file=sys.stderr)
            elif len(parts) == 2:
                self.edge_parts[id] = tuple(parts)

    def __find_border_edges(self):
        border_edges = [[] for _ in range(self.num_parts)]
        
        for id in self.edge_parts:
            (p0, p1) = self.edge_parts[id]
            border_edge = self.__get_border_edge_data(id, p0, p1)
            border_edges[p0].append(border_edge)
            border_edges[p1].append(border_edge)
            
        return border_edges

    def __get_border_edge_data(self, edge_id: str, part0: int, part1: int):
        border_edge = {}
        border_edge["id"] = edge_id
        net_file = self.netfiles[part0]
        root = net_file.getroot()
        for el in root.findall("edge"):
            if edge_id == el.get("id"):
                border_edge["lanes"] = [lane_el.get("id") for lane_el in el.findall("lane")]
                from_junc = el.get("from")
                from_, to = None, None
                for jun_el in root.findall("junction"):
                    if from_junc == jun_el.get("id"):
                        if jun_el.get("type") == "dead_end":
                            from_, to = part1, part0
                        else:
                            from_, to = part0, part1
                        border_edge["from"] = from_
                        border_edge["to"] = to
                        break
        return border_edge

    def __find_part_neighbors(self):
        part_neighbor_sets = [set() for _ in range(self.num_parts)]
        for id in self.edge_parts:
            (p0, p1) = self.edge_parts[id]
            part_neighbor_sets[p0].add(p1)
            part_neighbor_sets[p1].add(p0)
        return [list(s) for s in part_neighbor_sets]
    
    def __get_routes(self, neighbor_lists):
        part_routes = [[] for _ in range(self.num_parts)]
        for part_idx in range(self.num_parts):
            route_path = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))
            route_file = ET.parse(route_path)
            root = route_file.getroot()
            for route in root.findall("route"):
                part_routes[part_idx].append(route.attrib['id'])
                
        part_neighbor_routes = [
            {neigh_id: part_routes[neigh_id] for neigh_id in neighbor_lists[part_idx]} 
            for part_idx in range(self.num_parts)
        ]
                
        return part_neighbor_routes

    def generate_partition_data(self):
        self.__load_edges()
        border_edges = self.__find_border_edges()
        neighbor_lists = self.__find_part_neighbors()
        part_neighbor_routes = self.__get_routes(neighbor_lists)
        self.__save_to_json_(border_edges, neighbor_lists, part_neighbor_routes)
        print(f"Saved edge data to json for {self.num_parts} partitions")
        
    def __save_to_json_(self, border_edges, neighbor_lists, part_neighbor_routes):
        for part_id in range(self.num_parts):
            path = os.path.join(self.data_folder, f"partData{part_id}.json")
            with open(path, 'w') as f:
                json.dump({
                    'id': part_id,
                    'borderEdges': border_edges[part_id],
                    'neighbors': neighbor_lists[part_id],
                    'neighborRoutes': part_neighbor_routes[part_id],
                }, f)