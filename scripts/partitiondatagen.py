"""
processNeighbors.py

Find partition neighbors and process shared edge data, to be
used in ParallelTwin. Should be used after createParts.py
generated the required files.
"""

from xml.etree import ElementTree as ET
from collections import defaultdict
import os, sys
import json
import re

class PartitionDataGen:
    num_parts: int
    netfiles: dict[int, ET.ElementTree]
    routefiles: dict[int, ET.ElementTree]
    edge_parts: dict[str, tuple[int, int]]
    data_folder: str
    
    def __init__(self, num_parts: int, data_folder: str = "data"):
        self.num_parts = num_parts
        self.netfiles = {}
        self.routefiles = {}
        self.edge_parts = defaultdict(tuple)
        self.data_folder = data_folder

    def __load(self):
        self.edge_parts = defaultdict(tuple)
        all_edges_l = defaultdict(list)
        for part_idx in range(self.num_parts):
            net_path = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.net.xml"))
            net_file = ET.parse(net_path)
            route_path = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))
            route_file = ET.parse(route_path)
            self.netfiles[part_idx] = net_file
            self.routefiles[part_idx] = route_file
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

    def __find_border_edges(self) -> list[list[dict]]:
        border_edges = [[] for _ in range(self.num_parts)]
        
        for id in self.edge_parts:
            (p1, p2) = self.edge_parts[id]
            edge_border_edges = self.__get_border_edge_data(id, p1, p2)
            border_edges[p1].extend(edge_border_edges[0])
            border_edges[p2].extend(edge_border_edges[1])
            
        return border_edges

    def __get_border_edge_data(self, edge_id: str, part1: int, part2: int) -> tuple[list[dict], list[dict]]:
        net_file = self.netfiles[part1]
        root = net_file.getroot() 
        edge_el = root.find(f".//edge[@id='{edge_id}']")
        if edge_el:
            # temp: add both ways to both partitions always
            l =[{
                "id": edge_id,
                "lanes": [lane_el.get("id") for lane_el in edge_el.findall("lane")],
                "from": part1 if i % 2 == 0 else part2,
                "to": part1 if i % 2 == 1 else part2,
            } for i in range(2)]
            return l, l
            
            # this currently checks for dead ends only; doesn't include traffic_light, which
            # seems like it also can be a border from experimentation;
            # TODO: improve if needed, if not keep adding both ways as edges
            # from_junc = edge_el.get("from")
            # from_, to = None, None
            # jun_el = root.find(f".//junction[@id='{from_junc}']")
            # if jun_el:
            #     if jun_el.get("type") == "dead_end":
            #         from_, to = part2, part1
            #     else:
            #         from_, to = part1, part2
            #     border_edge["from"] = from_
            #     border_edge["to"] = to
        raise Exception("Cannot find edge " + edge_id)

    def __find_part_neighbors(self):
        part_neighbor_sets = [set() for _ in range(self.num_parts)]
        for id in self.edge_parts:
            (p1, p2) = self.edge_parts[id]
            part_neighbor_sets[p1].add(p2)
            part_neighbor_sets[p2].add(p1)
        return [list(s) for s in part_neighbor_sets]
    
    def __get_routes(self, neighbor_lists):
        part_routes = [[] for _ in range(self.num_parts)]
        for part_idx in range(self.num_parts):
            route_file = self.routefiles[part_idx]
            root = route_file.getroot()
            for route in root.findall("route"):
                route_id = route.attrib['id']
                id_no_part = re.sub(r'_part\d+', '', route_id)
                part_routes[part_idx].append(id_no_part)
                
        part_neighbor_routes = [
            {neigh_id: part_routes[neigh_id] for neigh_id in neighbor_lists[part_idx]} 
            for part_idx in range(self.num_parts)
        ]
                
        return part_neighbor_routes
    
    def __get_route_ends(self, border_edges: list[list[dict]]):
        # for every border edge in every part, 
        # get the routes it is the end of
        # (saving this way is more efficient during sim)
        part_edge_route_ends = [defaultdict(list) for _ in range(self.num_parts)]
        
        for (part_idx, border_edges_ls) in enumerate(border_edges):
            edge_route_ends = part_edge_route_ends[part_idx]
            route_file = self.routefiles[part_idx]
            root = route_file.getroot()
            
            for route in root.findall("route"):
                route_id = route.attrib['id']
                id_no_part = re.sub(r'_part\d+', '', route_id)
                route_edges = route.attrib["edges"].split()
                route_end = route_edges[-1]

                edge = next((edge for edge in border_edges_ls if edge["id"] == route_end), None)
                if edge:
                    edge_route_ends[edge["id"]].append(id_no_part)
                
        return part_edge_route_ends
    
    def __get_last_depart_times(self):
        out = []
        for part_idx in range(self.num_parts):
            route_file = self.routefiles[part_idx]
            root = route_file.getroot()
            val = 0
            for el in root:
                # depart in single elements, end in flow elements
                if "depart" in el.attrib or "end" in el.attrib:
                    time = float(el.attrib.get("depart", el.attrib.get("end", None)))  
                    val = max(time, val)
                    
            out.append(val)
        return out

    def generate_partition_data(self):
        self.__load()
        border_edges = self.__find_border_edges()
        neighbor_lists = self.__find_part_neighbors()
        part_neighbor_routes = self.__get_routes(neighbor_lists)
        part_route_ends = self.__get_route_ends(border_edges)
        part_last_depart_times = self.__get_last_depart_times()
        
        for part_id in range(self.num_parts):
            path = os.path.join(self.data_folder, f"partData{part_id}.json")
            with open(path, 'w') as f:
                json.dump({
                    'id': part_id,
                    'borderEdges': border_edges[part_id],
                    'neighbors': neighbor_lists[part_id],
                    'neighborRoutes': part_neighbor_routes[part_id],
                    'borderRouteEnds': part_route_ends[part_id],
                    'lastDepart': part_last_depart_times[part_id],
                }, f)
                
        print(f"Saved edge data to json for {self.num_parts} partitions")