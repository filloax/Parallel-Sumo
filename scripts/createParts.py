"""
Python adaptation of the C++ code originally in 
ParallelSim.cpp to partition a SUMO network and
generate support files for that program to run.

Original code by Phillip Taylor
Adapted by Filippo Lenzi
"""

if __name__ == '__main__':
    "Initializing partitioning script..."

import itertools
import os, sys
import glob
import lxml.etree
from lxml.etree import _Element as Element, _ElementTree as ElementTree
import argparse
import re
import contextily as cx
from threading import Thread, Lock, get_ident
from prefixing import ThreadPrefixStream
from multiprocessing import Process
from multiprocessing.pool import ThreadPool
from typing import Callable, TypedDict
from collections import defaultdict
from time import time
from datetime import timedelta
import json

from convertToMetis import main as convert_to_metis, weight_funs, WEIGHT_ROUTE_NUM, node_weight_funs
from sumobin import run_duarouter, run_netconvert
from sumo2png import generate_network_image, generate_partitions_image
from partitiondatagen import PartitionDataGen
from partroutes import part_route

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    route_tools = os.path.join(SUMO_HOME, 'tools', 'route')
    net_tools = os.path.join(SUMO_HOME, 'tools', 'net')
    tools = os.path.join(os.environ['SUMO_HOME'], 'tools')
    sys.path.append(os.path.join(tools))
    sys.path.append(os.path.join(route_tools))

    from cutRoutes import main as cut_routes, get_options as cut_routes_options
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

def check_nparts(value):
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError(f"{value} is an invalid thread num (positive int value)")
    return ivalue

def filter_vehs(value):
    return value if re.match(r'(\w+(,\w+)*)?', value) else None

parser = argparse.ArgumentParser()
parser.add_argument('-N', '--num-parts', required=True, type=check_nparts)
parser.add_argument('-c', '--cfg-file', required=True, type=str, help="Path to the SUMO .sumocfg simulation config")
parser.add_argument('--data-folder', default='data', help="Folder to store output in")
parser.add_argument('--keep-poly', action='store_true', help="Keep poly files from the sumocfg (disabled by default for performance)")
parser.add_argument('--no-metis', action='store_true', help="Partition network using grid (unsupported)")
parser.add_argument('-w', '--weight-fun', choices=weight_funs, nargs="*", default=[WEIGHT_ROUTE_NUM], help="One or more weighting methods to use, use with no values to avoid using any weight.")
parser.add_argument('-W', '--node-weight', choices=node_weight_funs, nargs="*", default=[], help="One or more weighting methods to use" \
    " for nodes, use with no values to avoid using any weight.")
parser.add_argument('-T', '--threads', type=int, default=8, help="Threads to use for processing the partitioning of the network, will be capped to partition num")
# remove default True later
parser.add_argument('--use-cut-routes', action='store_true', help="Use cutRoutes.py with postprocessing instead of our custom script to cut the routes (probably slower, but might handle different cases)")
parser.add_argument('-t', '--timing', action='store_true', help="Measure the timing for the whole process")
# parser.add_argument('--dev-mode', action='store_false', help="Remove some currently unhandled edge cases from the routes (not ideal in release, currently works inversely for easier development)")
parser.add_argument('--png', action='store_true', help="Output network images for each partition")
parser.add_argument('--quick-png', action='store_true', help="Remove some image details to output network images faster")
parser.add_argument('--force', action='store_true', help="Regenerate even if data folder already contains partition data matching these settings")
parser.add_argument('--filter-vehs', type=filter_vehs, default="", help="Test: Only keep vehicles with these ids in simulation")
parser.add_argument('-v', '--verbose', action='store_true', help="Additional output")

verbose = False
# devmode = False

def _is_poly_file(path: str):
    return path.endswith(".poly.xml")

class TestOptions(TypedDict):
    filter_vehicles: list[str]

class NetworkPartitioning:   
    def __init__(self, 
        cfg_file: str,
        use_metis: bool = True,
        data_folder: str = "data",
        keep_poly: bool = False,
        png: bool = False,
        quick_png: bool = False,
        weight_functions: list[str] = [WEIGHT_ROUTE_NUM],
        node_weight_functions: list[str] = [],
        threads: int = 1,
        timing: bool = False,
        use_cut_routes: bool = False,
        test_options: TestOptions = {},
    ) -> None:
        self.cfg_file = cfg_file
        self.use_metis = use_metis
        self.data_folder = data_folder
        self.keep_poly = keep_poly
        self.png = png
        self.quick_png = quick_png
        self.weight_functions = weight_functions
        self.node_weight_functions = node_weight_functions
        self.threads = threads
        self.timing = timing
        self.use_cut_routes = use_cut_routes
        self.test_options = test_options
        
        cfg_tree: ElementTree = lxml.etree.parse(self.cfg_file)
        cfg_root: Element = cfg_tree.getroot()
        cfg_dir = os.path.dirname(self.cfg_file)

        self.net_file: str = os.path.join(cfg_dir, cfg_root.find("./input/net-file").attrib["value"])
        self.route_files: list[str] = [os.path.join(cfg_dir, x) for x in cfg_root.find("./input/route-files").attrib["value"].split(",")]
        additional_files_el = cfg_root.find("./input/additional-files")
        self.additional_files: list[str] = [os.path.join(cfg_dir, x) for x in additional_files_el.attrib["value"].split(",")] \
            if additional_files_el is not None else []
        if not self.keep_poly:
            self.additional_files = list(filter(lambda x: not _is_poly_file(x), self.additional_files))

        self._temp_files = set()
        self._temp_files_lock = Lock()
        self._vehicle_depart_times = None
        
    def partition_network(self, num_parts: int):
        if self.use_cut_routes: # and devmode:
            print("-------------------------------")
            print("--          DEV MODE         --")
            print("-- Some edge cases will get  --")
            print("-- removed from the input.   --")
            print("-- * Routes/vehicles split   --")
            print("--   by cutRoutes.py         --")
            print("-------------------------------")

        if self.timing:
            start_t = time()

        os.makedirs(self.data_folder, exist_ok=True)
        for f in glob.glob(f'{self.data_folder}/**'):
            if os.path.basename(f) != "cache" and os.path.isfile(f):
                os.remove(f)

        # Preprocess routes file for proper input to cutRoutes.py
        processed_routes_path = self._preprocess_routes()

        print("Parsing network file...")

        # Load network XML
        network_tree: ElementTree = lxml.etree.parse(self.net_file)
        network: Element = network_tree.getroot()

        part_bounds_list = []
        netconvert_options = tuple([])
        edge_weights_file = os.path.join("data", "edge_weights.json")

        # Partition network with METIS
        if self.use_metis:
            print("Running convertToMetis.py to split graph...")
            print("=========================================")
            print()

            convert_to_metis(
                self.net_file, num_parts, 
                routefile=processed_routes_path,
                weight_functions=self.weight_functions,
                node_weight_functions=self.node_weight_functions,
                output_weights_file=edge_weights_file,
            )

            print()
            print("=========================================")

            # Read actual partition num from METIS output
            part_num_file = os.path.join(self.data_folder, "numParts.txt")
            try:
                with open(part_num_file, "r") as f:
                    number = int(f.read().strip())
                    num_parts = number
                    print(f"Set numThreads to {num_parts} from METIS output")
            except FileNotFoundError:
                print("Failed to open metis output partition num file.")
            except ValueError:
                print("Failed to read metis output partition num from file.")

            netconvert_options += ("--keep-edges.input-file",)
        else:
            # not tested as unused in original repo at time of forking
            locEl = network.find("location")
            boundText = locEl.get("convBoundary")
            bound = [int(val) for val in boundText.split(",")]
            xCenter = (bound[0] + bound[2]) // 2
            yCenter = (bound[1] + bound[3]) // 2

            if num_parts == 2:
                part_bounds_list.append(f"{bound[0]},{bound[1]},{xCenter},{bound[2]}")
                part_bounds_list.append(f"{xCenter},{bound[1]},{bound[2]},{bound[2]}")

            netconvert_options += ("--keep-edges.in-boundary",)

        # Reset holder variables
        self.min_depart_times = {}
        self.min_depart_times_lock = Lock()
        self._temp_files = set()
        self._temp_files_lock = Lock()

        # Make immutable for multithreading
        part_bounds = tuple(part_bounds_list)
                
        thread_num = min(num_parts, self.threads)
        if thread_num < num_parts:
            print(f"Reduced thread num to part num ({thread_num} instead of {self.threads})")

        # Run the partitioning proces
        if thread_num > 1:
            print(f"Running partition processing with {thread_num} threads...")
        else:
            print("Running partition processing single-threaded...")
        
        def process_part_work(part_idx: int):
            # Note that all arguments are immutable, to be thread-safe
            return self._process_partition(part_idx, processed_routes_path, netconvert_options, part_bounds)

        # ThreadPool (instead of Pool) to make sharing objects etc. possible
        with ThreadPool(thread_num) as pool:
            chunksize = num_parts // thread_num

            if self.use_cut_routes:
                partition_vehicle_start_times = pool.map(process_part_work, range(num_parts), chunksize)
                # First reduce into a smaller list in multiple threads,
                # then do final merging in main thread
                interm_dicts = pool.map(_merge_float_dicts_min, _split_list(partition_vehicle_start_times, thread_num), chunksize=1)
                self._vehicle_depart_times = _merge_float_dicts_min(interm_dicts)

                pool.map(self._postprocess_partition, range(num_parts), chunksize)
             
            # Using our script, we don't need to remove duplicate vehicles after the fact
            else:
                pool.map(process_part_work, range(num_parts), chunksize)

            self._final_duplicates_check(num_parts)
            # TODO: check if still needed?
            # self._final_route_connection_check(num_parts)
            
            belonging = pool.map(self._read_partition_vehicles, range(num_parts), chunksize)

            shared = set()
            for ls1, ls2 in itertools.product(belonging, belonging):
                if ls1 != ls2:
                    for id in ls1:
                        if id in ls2:
                            shared.add(id)

            if len(shared) > 0:
                print(f'[WARN] Vehicles shared between partitions: {shared}', file=sys.stderr)

            if verbose:
                for part_idx, ls in enumerate(belonging):
                    ls.sort(key=lambda x: int(re.sub(r'[^\d]', '', x)))
                    print(f"Vehicles starting in partition {part_idx} ({len(ls)}): [ ", ', '.join(ls), "]")


        if self.png:
            self.generate_images(num_parts)
            
        print("Generating edge data json...")
        postprocessor = PartitionDataGen(num_parts, self.data_folder)
        postprocessor.generate_partition_data()
        
        print("Cleaning up temp files...")

        for file in self._temp_files:
            os.remove(file)

        print("Finished partitioning network!")

        if self.timing:
            end_t = time()
            print(f"Took {timedelta(seconds=(end_t - start_t))}")

    def generate_images(self, num_parts: int):
        edge_weights_file = os.path.join("data", "edge_weights.json")
        generate_partitions_image([os.path.join(self.data_folder, f"part{i}.net.xml") for i in range(num_parts)], 
            os.path.join("output", f"partitions.png"),
            self.data_folder, 
            self._temp_files,
            not self.quick_png,
        )
        # weight color image
        generate_network_image([os.path.join(self.data_folder, f"part{i}.net.xml") for i in range(num_parts)], 
            os.path.join("output", f"partitions_weights.png"),
            self.data_folder, 
            self._temp_files,
            edge_weights_file,
        )

    def _preprocess_routes(self):
        interm_file_path = os.path.join(self.data_folder, "processed_routes.interm.rou.xml")
        processed_routes_path = os.path.join(self.data_folder, "processed_routes.rou.xml")

        filtered_files = [*self.route_files]

        if len(self.test_options["filter_vehicles"]) > 0:
            filter_dir = os.path.join(self.data_folder, "filtered_routes")
            os.makedirs(filter_dir, exist_ok=True)
            
            c = 0
            ff_v = []
            for file in filtered_files:
                fn, ext = os.path.splitext(os.path.basename(file))
                outf = os.path.join(filter_dir, f"{fn}-fv{ext}")
                
                orig_routes_tree: ElementTree = lxml.etree.parse(file)
                orig_routes: Element = orig_routes_tree.getroot()

                drop = []
                check_routes = set()
                for vehicle in itertools.chain(orig_routes.findall("vehicle"), orig_routes.findall("trip")):
                    if vehicle.attrib["id"] not in self.test_options["filter_vehicles"]:
                        drop.append(vehicle)
                        if "route" in vehicle.attrib:
                            check_routes.append(route_el)
                    else:
                        c += 1
                for vehicle in drop:
                    orig_routes.remove(vehicle)
                # Remove routes left with no vehicle
                for route in check_routes:
                    if not orig_routes.find(f""".//vehicle[@route='{route.attrib["id"]}']"""):
                        orig_routes.remove(route)
                        
                orig_routes_tree.write(outf, pretty_print=True)
                ff_v.append(outf)
                
            filtered_files = ff_v
                    
            print("Test: Kept", c, "vehicles after id filtering")
                    
        # duarouter is a SUMO executable that computes trips, aka SUMO routes defined only by start and end points
        # and normally computed via shortest-path at runtime
        # (This also ends up joining the input route files into one regardless)
        run_duarouter(self.net_file, filtered_files, interm_file_path, additional_files=self.additional_files, quiet=not verbose)
        # Remove alternate path files
        for f in glob.glob(f'{self.data_folder}/*.alt.xml'):
            os.remove(f)

        routes_tree: ElementTree = lxml.etree.parse(interm_file_path)
        routes: Element = routes_tree.getroot()

        # Takes routes defined inside of vehicles out of the vehicles
        count = 0
        for vehicle in routes.findall("vehicle"):
            route_el = vehicle.find("route")
            if route_el is not None:
                vehicle_id = vehicle.attrib.get("id", -1)
                id = f"vr_{vehicle_id}_{count}"
                route_ref_el = lxml.etree.Element("route")
                route_ref_el.set("id", id)
                route_ref_el.set("edges", route_el.get("edges"))
                vehicle.set("route", id)
                routes.append(route_ref_el)
                vehicle.remove(route_el)
                count += 1

        routes_tree.write(processed_routes_path, encoding='utf-8')

        return processed_routes_path

    def _process_partition(self, 
        part_idx: int,
        processed_routes_path: str,
        netconvert_options_base: tuple[str],
        part_bounds: tuple[str],
    ):
        if type(sys.stdout) is ThreadPrefixStream:
            sys.stdout.add_thread_prefix(f"[Thread {get_ident():5}]")

        cfg_dir = os.path.dirname(self.cfg_file)

        net_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.net.xml"))
        interm_rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.interm.rou.xml"))
        rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))
        cfg_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.sumocfg"))

        netconvert_options = list(netconvert_options_base)
        if self.use_metis:
            netconvert_options.append(os.path.join(self.data_folder, f"edgesPart{part_idx}.txt"))
        else:
            netconvert_options.append(part_bounds[part_idx])

        # Create partition
        print(f"Running netConvert for partition {part_idx}...")
        run_netconvert(net_file=self.net_file, output=net_part, extra_options=netconvert_options, mute_warnings=not verbose)
        print(f"Partition {part_idx} successfully created")

        if self.use_cut_routes:
            print(f"Running cutRoutes.py to create routes for partition {part_idx}...")
            # Put output of cut_routes in temp file, edit it, then save it
            cut_routes(cut_routes_options([
                net_part, processed_routes_path,
                "--routes-output", interm_rou_part,
                "--orig-net", self.net_file,
                # "--disconnected-action", "keep" # TEMP, TODO: re enable when split routes handled
            ]))

            vehicle_depart_times: dict[str, float] = {}
            
            with open(interm_rou_part, 'r', encoding='utf-8') as f:
                route_part_el: Element = lxml.etree.parse(f).getroot()
                vehicles = route_part_el.findall("vehicle")
                for vehicle in vehicles:
                    id = vehicle.attrib["id"]
                    depart = float(vehicle.attrib["depart"])

                    if id not in vehicle_depart_times:
                        vehicle_depart_times[id] = depart
                    # Shouldn't happen (means vehicle has duplicate id), but check anyways
                    elif vehicle_depart_times[id] > depart:
                        vehicle_depart_times[id] = depart
 
            # Do all non-thread-safe/locking operations at the end
            with self._temp_files_lock:
                self._temp_files.add(interm_rou_part)
        else:
            print(f"Cutting route with partroutes for partition {part_idx}")
            # note that we don't need postprocessing using our script, as it already
            # avoids repeating the same vehicle etc
            part_route(processed_routes_path, net_part, rou_part)

        # Create sumo cfg file for partition
        with open(self.cfg_file, "r") as source, open(cfg_part, "w") as dest:
            for line in source:
                dest.write(line)

        # Set partition net-file and route-files in cfg file
        cfg_part_tree: ElementTree = lxml.etree.parse(cfg_part)
        cfg_part_el: Element = cfg_part_tree.getroot()
        parent_map = {c:p for p in cfg_part_tree.iter() for c in p}
        input_el = cfg_part_el.find("input")
        net_file_el = input_el.find("net-file")
        rou_file_el = input_el.find("route-files")

        net_file_el.set("value", net_part)
        rou_file_el.set("value", rou_part)

        elements_to_fix = [
            "gui-settings-file",
            "additional-files"
        ]

        for el_name in elements_to_fix:
            el = cfg_part_el.find(f'.//{el_name}')

            if el is not None:
                prev_val = el.get("value")
                files = prev_val.split(",")
                fixed_files = [os.path.abspath(os.path.join(cfg_dir, val)) for val in files]
                el.set("value", ",".join(fixed_files))

        # Remove poly
        if not self.keep_poly:
            additional_files = cfg_part_el.find(f'.//additional-files')
            if additional_files is not None:
                prev_val = additional_files.get("value")
                files = prev_val.split(",")
                additional_files.set("value", ",".join(list(filter(lambda x: not _is_poly_file(x), files))))
                if additional_files.get("value") == "":
                    parent_map[additional_files].remove(additional_files)

        cfg_part_tree.write(cfg_part)

        if self.use_cut_routes:
            return vehicle_depart_times
        else:
            None
            
    def _postprocess_partition(self, part_idx: int):
        if type(sys.stdout) is ThreadPrefixStream:
            sys.stdout.add_thread_prefix(f"[Post process: {get_ident():5}]")

        interm_rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.interm.rou.xml"))
        rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))

        with open(interm_rou_part, 'r', encoding='utf-8') as fr:
            route_part_tree: ElementTree = lxml.etree.parse(fr)
            route_part_el: Element = route_part_tree.getroot()
            vehicles = route_part_el.findall("vehicle")
            remove = []
            for vehicle in vehicles:
                id = vehicle.attrib["id"]
                depart = float(vehicle.attrib["depart"])
                if depart > self._vehicle_depart_times[id] + 0.001:
                    remove.append(vehicle)

            # Duplicate routes (likely unhandled edge cases in partitioning and cutting)
            routes = route_part_el.findall("route")
            found_rids = set()
            dup_route_removed = 0
            for route in routes:
                id = route.attrib["id"]
                if id not in found_rids:
                    found_rids.add(id)
                else:
                    remove.append(route)
                    dup_route_removed += 1

            for el in remove: 
                route_part_el.remove(el)
            route_part_tree.write(rou_part)

            if dup_route_removed > 0:
                print(f"Removed {dup_route_removed} duplicate routes (likely unhandled edge cases)")

    def _get_route_for_id(self, routes_root: Element, id: str):
        return routes_root.xpath(f".//route[@id='{id}' or @id_og='{id}']")[0]

    # In case route parts were changed by a script part, fix "holes"
    def _fix_multipart_route_ids(self, routes_el: Element):
        route_nodes: list[Element] = routes_el.findall("route")
        route_nodes.sort(key=lambda route: route.attrib["id"])
        last_part_num = defaultdict(lambda: -1)
        
        for route in route_nodes:
            id: str = route.attrib["id"]
            if '_part' in id:
                [prefix, numPart] = id.split('_part')
                num = int(numPart)
                if num > last_part_num[prefix] + 1:
                    num = last_part_num[prefix] + 1
                    route.set("id", f"{prefix}_part{num}")
                last_part_num[prefix] = num

    def _final_duplicates_check(self, num_parts):
        """Check for duplicates not caught by starting time check, 
        might be caused by vehicles starting in border edges.
        """
        route_parts = [os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml")) for part_idx in range(num_parts)]
        
        # Keep the vehicle with the longest route
        # (if a edge was split with a vehicle starting there, 
        # it will likely get a minimal route included in the one
        # in its copy in the neighbor partition)
        vehicle_route_lengths = defaultdict(list)
        for rou_part in route_parts:            
            with open(rou_part, 'r', encoding='utf-8') as f:
                route_part_el: Element = lxml.etree.parse(f).getroot()
                
            for veh in route_part_el.findall('vehicle'):
                id = veh.attrib["id"]
                route_id = veh.attrib["route"]
                route_el = self._get_route_for_id(route_part_el, route_id)
                route_len = len(route_el.attrib["edges"].split(" "))
                vehicle_route_lengths[id].append(route_len)
        
        # After checking, keep only the ones with longest route
        for i, rou_part in enumerate(route_parts):
            with open(rou_part, 'r', encoding='utf-8') as f:
                tree = lxml.etree.parse(f)
                route_part_el: Element = tree.getroot()
            
            delete = []
            for veh in route_part_el.findall('vehicle'):
                id = veh.attrib["id"]
                route_id = veh.attrib["route"]
                route_el = self._get_route_for_id(route_part_el, route_id)
                route_len = len(route_el.attrib["edges"].split(" "))
                max_len = max(vehicle_route_lengths[id])
                
                rem = route_len < max_len
                # edge case: if somehow (depends on SUMO cutRoutes) there
                # are routes in different parts with the same edges, 
                # remove all but one (arbitrarily)
                rem = rem or len([x for x in vehicle_route_lengths[id] if x == max_len]) > 1
                if rem:
                    # directly remove route here as it doesn't affect iterator
                    route_part_el.remove(route_el)
                    delete.append(veh)
                    vehicle_route_lengths[id].remove(route_len)

            # Delete in separate loop to avoid messing with the iterator
            for veh in delete:
                route_part_el.remove(veh)
                   
            self._fix_multipart_route_ids(route_part_el)
             
            tree.write(rou_part, encoding='utf-8')
                
            print(f"Partition {i}: removed {len(delete)} additional duplicate vehicles")

    def __each_shares_element_with_prec(self, lst: list[list]):
        return all(set(lst[i]) & set(lst[i - 1]) for i in range(1, len(lst)))

    def _final_route_connection_check(self, num_parts: int):
        route_parts = [os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml")) for part_idx in range(num_parts)]
        route_segs_by_id = defaultdict(list)
        
        for file in route_parts:
            tree = lxml.etree.parse(file)
            root: Element = tree.getroot()
            for route in root.findall("route"):
                route_segs_by_id[route.attrib["id"]].append(route.attrib["edges"].split(" "))

        matching = []

        for id in route_segs_by_id:
            lists = route_segs_by_id[id]
            if not self.__each_shares_element_with_prec(lists):
                matching.append(id)
                
        for file in route_parts:
            tree: ElementTree = lxml.etree.parse(file)
            root: Element = tree.getroot()
            for id in matching:
                for route in root.findall(f".//route[@id='{id}']"):
                    root.remove(route)
                for veh in root.findall(f".//vehicle[@route='{id}']"):
                    root.remove(veh)
       
            tree.write(file, encoding='utf-8')
                
        if len(matching) > 0:
            print(f"[WARN] {len(matching)} routes with non continuous edges", file=sys.stderr)

    def _read_partition_vehicles(self, part_idx):
        rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))
        with open(rou_part, 'r', encoding='utf-8') as f:
            route_part_el: Element = lxml.etree.parse(f).getroot()
        return [el.attrib['id'] for el in route_part_el.findall(f'.//vehicle')]

def _get_inf():
    return float("inf")

def _merge_float_dicts_min(dicts: list[dict[str, float]]) -> dict[str, float]:
    result_dict = defaultdict(_get_inf)
    
    for input_dict in dicts:
        for key, value in input_dict.items():
            result_dict[key] = min(result_dict[key], value)
    
    return dict(result_dict)

def _split_list(list: list, n: int) -> list[list]:
    part_size = len(list) // n
    remainder = len(list) % n

    parts = []
    start = 0

    for i in range(n):
        end = start + part_size + (1 if i < remainder else 0)
        parts.append(list[start:end])
        start = end

    return parts

def _get_check_args(args: object):
    d = args.__dict__.copy()
    del d["verbose"]
    del d["force"]
    del d["threads"]
    del d["timing"]
    del d["png"]
    del d["quick_png"]
    return d

def _save_args(args: object):
    args_path = os.path.join(args.data_folder, "partArgs.json")
    with open(args_path, 'w', encoding='utf-8') as f:
        json.dump(_get_check_args(args), f)
        
def _check_args(args: object):
    args_path = os.path.join(args.data_folder, "partArgs.json")
    try:
        if os.path.exists(args_path):
            with open(args_path, 'r', encoding='utf-8') as f:
                old_args = json.load(f)
            return old_args == _get_check_args(args)
    except Exception:
        print("Coudldn't check for previous calls' args", file=sys.stderr)
    return False

def worker(args):
    global verbose #, devmode

    if args.verbose:
        verbose = True

    # if args.dev_mode:
    #     devmode = True

    print(args)

    cache_dir = os.path.join(args.data_folder, "cache")
    cx.set_cache_dir(cache_dir)
    os.makedirs(cache_dir, exist_ok=True)
    os.makedirs("output", exist_ok=True)

    if args.threads > 1:
        sys.stdout = ThreadPrefixStream()

    print("Initializing network partitioning...")

    partitioning = NetworkPartitioning(
        args.cfg_file,
        not args.no_metis,
        args.data_folder,
        args.keep_poly,
        args.png,
        args.quick_png,
        args.weight_fun,
        args.node_weight,
        args.threads,
        args.timing,
        args.use_cut_routes,
        {
            "filter_vehicles": [v for v in args.filter_vehs.split(",") if v != ""]
        },
    )
    
    if not args.force and _check_args(args):
        print("Partitioning same as previous gen in this folder, skipping (use --force to ignore this and run anyways)")
        if args.png:
            partitioning.generate_images(args.num_parts)
    else:
        partitioning.partition_network(args.num_parts)
        _save_args(args)

def main(args):
    worker_process = Process(target=worker, args=[args])
    worker_process.start()

    try:
        while worker_process.is_alive():
            worker_process.join(timeout=5)
    except KeyboardInterrupt:
        print("Quitting.")
        worker_process.terminate()

    sys.exit(worker_process.exitcode)

if __name__ == '__main__':
    main(parser.parse_args())