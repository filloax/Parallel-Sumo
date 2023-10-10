"""
Python adaptation of the C++ code originally in 
ParallelSim.cpp to partition a SUMO network and
generate support files for that program to run.

Original code by Phillip Taylor
Adapted by Filippo Lenzi
"""

import os, sys
import glob
import xml.etree.ElementTree as ET
from xml.etree.ElementTree import Element
import argparse
import re
import contextily as cx
from threading import Thread, Lock, get_ident
from prefixing import ThreadPrefixStream
from multiprocessing import Process
from multiprocessing.pool import ThreadPool
from typing import Callable
from collections import defaultdict
from time import time
from datetime import timedelta

from convertToMetis import main as convert_to_metis, weight_funs, WEIGHT_ROUTE_NUM
from sumobin import run_duarouter, run_netconvert
from sumo2png import generate_network_image

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    route_tools = os.path.join(SUMO_HOME, 'tools', 'route')
    net_tools = os.path.join(SUMO_HOME, 'tools', 'net')
    sys.path.append(os.path.join(route_tools))

    import sumolib  # noqa
    from cutRoutes import main as cut_routes, get_options as cut_routes_options

    DUAROUTER = sumolib.checkBinary('duarouter')
    NETCONVERT = sumolib.checkBinary('netconvert')
    # python script, but doesn't have an importable main function (everything in the if)
    NET2GEOJSON = os.path.join(net_tools, "net2geojson")
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

def check_nparts(value):
    ivalue = int(value)
    if ivalue <= 0:
        raise argparse.ArgumentTypeError(f"{value} is an invalid thread num (positive int value)")
    return ivalue

parser = argparse.ArgumentParser()
parser.add_argument('-n', '--num-parts', required=True, type=check_nparts)
parser.add_argument('-C', '--cfg-file', required=True, type=str, help="Path to the SUMO .sumocfg simulation config")
parser.add_argument('--data-folder', default='data', help="Folder to store output in")
parser.add_argument('--keep-poly', action='store_true', help="Keep poly files from the sumocfg (disabled by default for performance)")
parser.add_argument('--no-metis', action='store_true', help="Partition network using grid (unsupported)")
parser.add_argument('-w', '--weight-fun', choices=weight_funs, nargs="*", default=[WEIGHT_ROUTE_NUM], help="One or more weighting methods to use")
parser.add_argument('-nw', '--no-weight', action='store_true', help="Do not use edge weights in partitioning")
parser.add_argument('-T', '--threads', type=int, default=1, help="Threads to use for processing the partitioning of the network")
# remove default True later
parser.add_argument('-t', '--timing', action='store_true', help="Measure the timing for the whole process")
parser.add_argument('--dev-mode', action='store_false', help="Remove some currently unhandled edge cases from the routes (not ideal in release, currently works inversely for easier development)")
parser.add_argument('--png', action='store_true', help="Output network images for each partition")
parser.add_argument('-v', '--verbose', action='store_true', help="Additional output")

verbose = False
devmode = False

def _is_poly_file(path: str):
    return path.endswith(".poly.xml")

class NetworkPartitioning:
    def __init__(self, 
        cfg_file: str,
        use_metis: bool = True,
        data_folder: str = "data",
        keep_poly: bool = False,
        png: bool = False,
        weight_functions: list[str] = [WEIGHT_ROUTE_NUM],
        threads: int = 1,
        timing: bool = False,
    ) -> None:
        self.cfg_file = cfg_file
        self.use_metis = use_metis
        self.data_folder = data_folder
        self.keep_poly = keep_poly
        self.png = png
        self.weight_functions = weight_functions
        self.threads = threads
        self.timing = timing
        
        cfg_tree = ET.parse(self.cfg_file)
        cfg_root = cfg_tree.getroot()
        cfg_dir = os.path.dirname(self.cfg_file)

        self.net_file: str = os.path.join(cfg_dir, cfg_root.find("./input/net-file").attrib["value"])
        self.route_files: list[str] = [os.path.join(cfg_dir, x) for x in cfg_root.find("./input/route-files").attrib["value"].split(",")]
        additional_files_el = cfg_root.find("./input/additional-files")
        self.additional_files: list[str] = [os.path.join(cfg_dir, x) for x in additional_files_el.attrib["value"].split(",")] if additional_files_el else []
        if not self.keep_poly:
            self.additional_files = list(filter(lambda x: not _is_poly_file(x), self.additional_files))

        self._temp_files = set()
        self._temp_files_lock = Lock()
        self._vehicle_depart_times = None

    def partition_network(self, num_parts: int):
        if devmode:
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
        for f in glob.glob(f'{self.data_folder}/*'):
            if os.path.basename(f) != "cache":
                os.remove(f)

        # Preprocess routes file for proper input to cutRoutes.py
        processed_routes_path = self._preprocess_routes()

        print("Parsing network file...")

        # Load network XML
        network_tree = ET.parse(self.net_file)
        network = network_tree.getroot()

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

        # Run the partitioning proces
        if self.threads > 1:
            print(f"Running partition processing with {self.threads} threads...")
        else:
            print("Running partition processing single-threaded...")
        
        def process_part_work(part_idx: int):
            # Note that all arguments are immutable, to be thread-safe
            return self._process_partition(part_idx, processed_routes_path, netconvert_options, part_bounds)

        # ThreadPool (instead of Pool) to make sharing objects etc. possible
        with ThreadPool(self.threads) as pool:
            chunksize = num_parts // self.threads
            partition_vehicle_start_times = pool.map(process_part_work, range(num_parts), chunksize)

            # First reduce into a smaller list in multiple threads,
            # then do final merging in main thread
            interm_dicts = pool.map(_merge_float_dicts_min, _split_list(partition_vehicle_start_times, self.threads), chunksize=1)
            self._vehicle_depart_times = _merge_float_dicts_min(interm_dicts)
           
            print("Processing done, postprocessing...")

            pool.map(self._postprocess_partition, range(num_parts), chunksize)

            print("Postprocessing done")

        if self.png:
            generate_network_image([os.path.join(self.data_folder, f"part{i}.net.xml") for i in range(num_parts)], 
                os.path.join(self.data_folder, f"partitions.png"),
                self.data_folder, 
                self._temp_files,
            )
            # weight color image
            generate_network_image([os.path.join(self.data_folder, f"part{i}.net.xml") for i in range(num_parts)], 
                os.path.join(self.data_folder, f"partitions_weights.png"),
                self.data_folder, 
                self._temp_files,
                edge_weights_file,
            )
        
        print("Cleaning up temp files...")

        for file in self._temp_files:
            os.remove(file)

        print("Finished partitioning network!")

        if self.timing:
            end_t = time()
            print(f"Took {timedelta(seconds=(end_t - start_t))}")

    def _preprocess_routes(self):
        processed_routes_path = os.path.join(self.data_folder, "processed_routes.rou.xml")
        interm_file_path = os.path.join(self.data_folder, "processed_routes.interm.rou.xml")

        # duarouter is a SUMO executable that computes trips, aka SUMO routes defined only by start and end points
        # and normally computed via shortest-path at runtime
        # (This also ends up joining the input route files into one regardless)
        run_duarouter(self.net_file, self.route_files, interm_file_path, additional_files=self.additional_files)
        # Remove alternate path files
        for f in glob.glob(f'{self.data_folder}/*.alt.xml'):
            os.remove(f)

        routes_tree = ET.parse(interm_file_path)
        routes: ET.Element = routes_tree.getroot()

        # Takes routes defined inside of vehicles out of the vehicles
        count = 0
        for vehicle in routes.findall("vehicle"):
            route_el = vehicle.find("route")
            if route_el is not None:
                vehicle_id = vehicle.attrib.get("id", -1)
                id = f"vr_{vehicle_id}_{count}"
                route_ref_el = ET.Element("route")
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
        run_netconvert(net_file=self.net_file, output=net_part, extra_options=netconvert_options)
        print(f"Partition {part_idx} successfully created")

        print(f"Running cutRoutes.py to create routes for partition {part_idx}...")
        # Put output of cut_routes in temp file, edit it, then save it
        cut_routes(cut_routes_options([
            net_part, processed_routes_path,
            "--routes-output", interm_rou_part,
            "--orig-net", self.net_file,
            "--disconnected-action", "keep"
        ]))

        vehicle_depart_times: dict[str, float] = {}
        
        with open(interm_rou_part, 'r', encoding='utf-8') as f:
            route_part_el: Element = ET.parse(f).getroot()
            vehicles = route_part_el.findall("vehicle")
            for vehicle in vehicles:
                id = vehicle.attrib["id"]
                depart = float(vehicle.attrib["depart"])

                if id not in vehicle_depart_times:
                    vehicle_depart_times[id] = depart
                # Shouldn't happen (means vehicle has duplicate id), but check anyways
                elif vehicle_depart_times[id] > depart:
                    vehicle_depart_times[id] = depart

        # Create sumo cfg file for partition
        with open(self.cfg_file, "r") as source, open(cfg_part, "w") as dest:
            for line in source:
                dest.write(line)

        # Set partition net-file and route-files in cfg file
        cfg_part_tree = ET.parse(cfg_part)
        cfg_part_el = cfg_part_tree.getroot()
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

        # Do all non-thread-safe/locking operations at the end
        with self._temp_files_lock:
            self._temp_files.add(interm_rou_part)

        return vehicle_depart_times

    def _devmode_remove_vehicle(self, vehicle: Element):
        id = vehicle.attrib["id"]
        return re.search(r'_part\d+$', id)

    def _postprocess_partition(self, part_idx: int):
        if type(sys.stdout) is ThreadPrefixStream:
            sys.stdout.add_thread_prefix(f"[Post process: {get_ident():5}]")

        interm_rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.interm.rou.xml"))
        rou_part = os.path.abspath(os.path.join(self.data_folder, f"part{part_idx}.rou.xml"))
        
        belonging = []

        with open(interm_rou_part, 'r', encoding='utf-8') as fr:
            route_part_tree = ET.parse(fr)
            route_part_el: Element = route_part_tree.getroot()
            vehicles = route_part_el.findall("vehicle")
            remove = []
            dev_removed = 0
            for vehicle in vehicles:
                id = vehicle.attrib["id"]
                depart = float(vehicle.attrib["depart"])
                if depart > self._vehicle_depart_times[id]:
                    remove.append(vehicle)
                elif devmode and self._devmode_remove_vehicle(vehicle):
                    remove.append(vehicle)
                    dev_removed += 1
                else:
                    belonging.append(vehicle)

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

            if dev_removed > 0:
                print(f"Removed {dev_removed} vehicles as part of dev mode")
            if dup_route_removed > 0:
                print(f"Removed {dup_route_removed} duplicate routes (likely unhandled edge cases)")

        belonging.sort(key=lambda x: int(re.sub(r'[^\d]', '', x.attrib["id"])))

        if verbose:
            print(f"Vehicles starting in partition {part_idx}: [ ", ', '.join([v.attrib["id"] for v in belonging]), "]")

def _merge_float_dicts_min(dicts: list[dict[str, float]]) -> dict[str, float]:
    result_dict = defaultdict(float)
    
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

def worker(args):
    global verbose, devmode

    if args.verbose:
        verbose = True

    if args.dev_mode:
        devmode = True

    print(args)

    cache_dir = os.path.join(args.data_folder, "cache")
    cx.set_cache_dir(cache_dir)
    os.makedirs(cache_dir, exist_ok=True)

    if args.threads:
        sys.stdout = ThreadPrefixStream()

    weight_funs = args.weight_fun
    if args.no_weight:
        weight_funs = []

    print("Initializing network partitioning...")

    partitioning = NetworkPartitioning(
        args.cfg_file,
        not args.no_metis,
        args.data_folder,
        args.keep_poly,
        args.png,
        weight_funs,
        args.threads,
        args.timing,
    )
    
    partitioning.partition_network(args.num_parts)

def main(args):
    worker_process = Process(target=worker, args=[args])
    worker_process.start()

    try:
        while worker_process.is_alive():
            worker_process.join(timeout=5)
    except KeyboardInterrupt:
        print("Quitting.")
        worker_process.terminate()

if __name__ == '__main__':
    main(parser.parse_args())