"""
Python adaptation of the C++ code originally in 
ParallelSim.cpp to partition a SUMO network and
generate support files for that program to run.

Original code by Phillip Taylor
Adapted by Filippo Lenzi
"""

import os, sys
import glob
import subprocess
import xml.etree.ElementTree as ET
from xml.etree.ElementTree import Element
import argparse
import re
from convertToMetis import main as convert_to_metis

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    route_tools = os.path.join(SUMO_HOME, 'tools', 'route')
    sys.path.append(os.path.join(route_tools))

    import sumolib  # noqa
    from cutRoutes import main as cut_routes, get_options as cut_routes_options

    DUAROUTER = sumolib.checkBinary('duarouter')
    NETCONVERT = sumolib.checkBinary('netconvert')
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

parser = argparse.ArgumentParser()
parser.add_argument('-n', '--num-threads', required=True, type=int)
parser.add_argument('-C', '--cfg-file', required=True, type=str, help="Path to the SUMO .sumocfg simulation config")
parser.add_argument('--data-folder', default='data', help="Folder to store output in")
parser.add_argument('--keep-poly', action='store_true', help="Keep poly files from the sumocfg (disabled by default for performance)")
parser.add_argument('--no-metis', action='store_true', help="Partition network using grid (unsupported)")
# remove default True later
parser.add_argument('--dev-mode', action='store_true', default=True, help="Remove some currently unhandled edge cases from the routes (not ideal in release)")
parser.add_argument('-v', '--verbose', action='store_true', help="Additional output")

verbose = False
devmode = False

def _is_poly_file(path: str):
    return path.endswith(".poly.xml")

def partition_network(
    num_threads: int, cfg_file: str,
    use_metis: bool = True,
    data_folder: str = "data",
    keep_poly: bool = False,
):
    if devmode:
        print("-------------------------------")
        print("--          DEV MODE         --")
        print("-- Some edge cases will get  --")
        print("-- removed from the input.   --")
        print("-- * Routes/vehicles split   --")
        print("--   by cutRoutes.py         --")
        print("-------------------------------")

    os.makedirs(data_folder, exist_ok=True)
    for f in glob.glob(f'{data_folder}/*'):
        os.remove(f)

    cfg_tree = ET.parse(cfg_file)
    cfg_root = cfg_tree.getroot()
    cfg_dir = os.path.dirname(cfg_file)

    net_file: str = os.path.join(cfg_dir, cfg_root.find("./input/net-file").attrib["value"])
    route_files: list[str] = [os.path.join(cfg_dir, x) for x in cfg_root.find("./input/route-files").attrib["value"].split(",")]
    additional_files_el = cfg_root.find("./input/additional-files")
    additional_files: list[str] = [os.path.join(cfg_dir, x) for x in additional_files_el.attrib["value"].split(",")] if additional_files_el else []
    if not keep_poly:
        additional_files = list(filter(lambda x: not _is_poly_file(x), additional_files))

    # Load network XML
    network_tree = ET.parse(net_file)
    network = network_tree.getroot()

    part_bounds = []
    netconvert_options = []
    
    # Partition network with METIS
    if use_metis:
        print("Running convertToMetis.py to split graph...")
        
        convert_to_metis(net_file, num_threads)

        # Read actual partition num from METIS output
        part_num_file = os.path.join(data_folder, "numParts.txt")
        try:
            with open(part_num_file, "r") as f:
                number = int(f.read().strip())
                num_threads = number
                print(f"Set numThreads to {num_threads} from METIS output")
        except FileNotFoundError:
            print("Failed to open metis output partition num file.")
        except ValueError:
            print("Failed to read metis output partition num from file.")

        netconvert_options.append("--keep-edges.input-file")
    else:
        # not tested as unused in original repo at time of forking
        locEl = network.find("location")
        boundText = locEl.get("convBoundary")
        bound = [int(val) for val in boundText.split(",")]
        xCenter = (bound[0] + bound[2]) // 2
        yCenter = (bound[1] + bound[3]) // 2

        if num_threads == 2:
            part_bounds.append(f"{bound[0]},{bound[1]},{xCenter},{bound[2]}")
            part_bounds.append(f"{xCenter},{bound[1]},{bound[2]},{bound[2]}")

        netconvert_options.append("--keep-edges.in-boundary")

    # Preprocess routes file for proper input to cutRoutes.py
    processed_routes_path = _preprocess_routes(net_file, route_files, data_folder, additional_files)

    # vehicle_id: depart_time
    min_depart_times = {}
    temp_files = []

    for i in range(num_threads):
        _process_partition(
            i, cfg_dir, cfg_file, net_file,
            processed_routes_path, netconvert_options.copy(),
            part_bounds, min_depart_times, temp_files,
            data_folder, use_metis, 
            keep_poly,
        )
        
    for i in range(num_threads):
        _postprocess_partition(i, min_depart_times, data_folder)
    
    for file in temp_files:
        os.remove(file)

def _preprocess_routes(
    net_file: str,
    route_files: list[str], 
    data_folder: str,
    additional_files: list[str] = [],
):
    processed_routes_path = os.path.join(data_folder, "processed_routes.rou.xml")
    interm_file_path = os.path.join(data_folder, "processed_routes.interm.rou.xml")

    # duarouter is a SUMO executable that computes trips, aka SUMO routes defined only by start and end points
    # and normally computed via shortest-path at runtime
    # (This also ends up joining the input route files into one regardless)
    _run_duarouter(net_file, route_files, interm_file_path, additional_files=additional_files)
    # Remove alternate path files
    for f in glob.glob(f'{data_folder}/*.alt.xml'):
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

def _process_partition(
    part_idx: int, cfg_dir: str, cfg_file: str, net_file: str,
    processed_routes_path: str,
    netconvert_options: list[str],
    part_bounds: dict,
    min_depart_times: dict, temp_files: list,
    data_folder: str = "data", use_metis = True,
    keep_poly = False,
):
    net_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.net.xml"))
    interm_rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.interm.rou.xml"))
    rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.rou.xml"))
    cfg_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.sumocfg"))

    if use_metis:
        netconvert_options.append(os.path.join(data_folder, f"edgesPart{part_idx}.txt"))
    else:
        netconvert_options.append(part_bounds[part_idx])

    # Create partition
    print(f"Running netConvert for partition {part_idx}...")
    _run_netconvert(net_file=net_file, output=net_part, extra_options=netconvert_options)
    print(f"Partition {part_idx} successfully created")

    print(f"Running cutRoutes.py to create routes for partition {part_idx}...")
    # Put output of cut_routes in temp file, edit it, then save it
    cut_routes(cut_routes_options([
        net_part, processed_routes_path,
        "--routes-output", interm_rou_part,
        "--orig-net", net_file,
        "--disconnected-action", "keep"
    ]))
    # temp_files.append(interm_rou_part)
    
    with open(interm_rou_part, 'r', encoding='utf-8') as f:
        route_part_el: Element = ET.parse(f).getroot()
        vehicles = route_part_el.findall("vehicle")
        for vehicle in vehicles:
            id = vehicle.attrib["id"]
            depart = float(vehicle.attrib["depart"])
            if id not in min_depart_times:
                min_depart_times[id] = depart
            elif min_depart_times[id] > depart:
                min_depart_times[id] = depart

    # Create sumo cfg file for partition
    with open(cfg_file, "r") as source, open(cfg_part, "w") as dest:
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
    if not keep_poly:
        additional_files = cfg_part_el.find(f'.//additional-files')
        if additional_files is not None:
            prev_val = additional_files.get("value")
            files = prev_val.split(",")
            additional_files.set("value", ",".join(list(filter(lambda x: not _is_poly_file(x), files))))
            if additional_files.get("value") == "":
                parent_map[additional_files].remove(additional_files)

    cfg_part_tree.write(cfg_part)

def _run_netconvert(net_file: str, output: str, extra_options: list[str] = []):
    _run_prefix([
        NETCONVERT,
        *extra_options,
        "-s", net_file,
        "-o", output
    ], "netconvert | ")

def _run_duarouter(net_file: str, trip_files: list[str], output: str, additional_files: list[str] = [], extra_options: list[str] = []):
    opts = [
        DUAROUTER,
        "--net-file", net_file, 
        "--route-files", ",".join(trip_files),
        "--output", output,
        *extra_options
    ]
    if len(additional_files) > 0:
        opts.extend([
            "--additional-files", ",".join(additional_files),
        ])
    _run_prefix(opts, "duarouter | ")

def _run_prefix(args: list[str], prefix: str = "PROC | ", **kwargs):
    print("Executing", " ".join(args))

    process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kwargs)

    while True:
        stdout_line = process.stdout.readline()
        stderr_line = process.stderr.readline()

        if not stdout_line and not stderr_line:
            break

        if stdout_line:
            # Prepend your prefix to stdout lines and print them
            print(prefix + stdout_line, end="")

        if stderr_line:
            # Handle stderr separately if needed
            print(prefix +stderr_line, end="", file=sys.stderr)

    # Wait for the process to complete
    process.wait()

    # Check the return code
    if process.returncode != 0:
        raise Exception("Error: The command failed with a non-zero exit code: " + process.returncode)

def _devmode_remove_vehicle(vehicle: Element):
    id = vehicle.attrib["id"]
    return re.search(r'_part\d+$', id)

def _postprocess_partition(
    part_idx: int, min_depart_times: dict,
    data_folder: str = "data",
):
    interm_rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.interm.rou.xml"))
    rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.rou.xml"))
    
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
            if depart > min_depart_times[id]:
                remove.append(vehicle)
            elif devmode and _devmode_remove_vehicle(vehicle):
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

def main(args):
    global verbose, devmode

    if args.verbose:
        verbose = True

    if args.dev_mode:
        devmode = True

    partition_network(
        args.num_threads,
        args.cfg_file,
        not args.no_metis,
        args.data_folder,
    )

if __name__ == '__main__':
    main(parser.parse_args())