"""
Python adaptation of the C++ code originally in 
ParallelSim.cpp to partition a SUMO network and
generate support files for that program to run.

Original code by Phillip Taylor
Adapted by Filippo Lenzi
"""

import os, sys
import subprocess
import xml.etree.ElementTree as ET
from xml.etree.ElementTree import Element
import argparse
from convertToMetis import main as convert_to_metis

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    route_tools = os.path.join(SUMO_HOME, 'tools', 'route')
    sys.path.append(route_tools)
    from cutRoutes import main as cut_routes, get_options as cut_routes_options
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

parser = argparse.ArgumentParser()
parser.add_argument('-n', '--num-threads', required=True, type=int)
parser.add_argument('-C', '--cfg-file', required=True, type=str, help="Path to the SUMO .sumocfg simulation config")
parser.add_argument('-N', '--net-file', required=True, type=str, help="Path to the SUMO .net.xml network file")
parser.add_argument('-R', '--route-file', required=True, type=str, help="Path to the SUMO .rou.xml network demand file")
parser.add_argument('--data-folder', default='data', help="Folder to store output in")
parser.add_argument('--no-metis', action='store_true', help="Partition network using grid (unsupported)")

def partition_network(
    num_threads: int, net_file: str, route_file: str, cfg_file: str,
    net_convert_bin: str,
    use_metis: bool = True,
    data_folder: str = "data",
):
    cfg_dir = os.path.dirname(cfg_file)
    
    # Load network XML
    network_tree = ET.parse(net_file)
    network = network_tree.getroot()

    part_bounds = []
    netconvert_option1 = ""
    
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

        netconvert_option1 = "--keep-edges.input-file"
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

        netconvert_option1 = "--keep-edges.in-boundary"

    # Preprocess routes file for proper input to cutRoutes.py
    routes_tree = ET.parse(route_file)
    routes = routes_tree.getroot()

    count = 0
    for vehicle in routes.findall("vehicle"):
        route_el = vehicle.find("route")
        if route_el is not None:
            id = f"custom_route{count}"
            route_ref_el = ET.Element("route")
            route_ref_el.set("id", id)
            route_ref_el.set("edges", route_el.get("edges"))
            vehicle.set("route", id)
            routes.append(route_ref_el)
            routes.remove(route_el)
            count += 1

    processed_routes_path = os.path.join(data_folder, "processed_routes.xml")
    routes_tree.write(processed_routes_path)

    # vehicle_id: depart_time
    min_depart_times = {}
    temp_files = []

    for i in range(num_threads):
        _process_partition(
            i, cfg_dir, cfg_file, net_file,
            processed_routes_path, net_convert_bin, netconvert_option1,
            part_bounds, min_depart_times, temp_files,
            data_folder, use_metis, 
        )
        
    for i in range(num_threads):
        _postprocess_partition(i, min_depart_times, data_folder)
    
    for file in temp_files:
        os.remove(file)

def _process_partition(
    part_idx: int, cfg_dir: str, cfg_file: str, net_file: str,
    processed_routes_path: str,
    net_convert_bin: str, netconvert_option1: str,
    part_bounds: dict,
    min_depart_times: dict, temp_files: list,
    data_folder: str = "data", use_metis = True
):
    net_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.net.xml"))
    interm_rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.interm.rou.xml"))
    rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.rou.xml"))
    cfg_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.sumocfg"))

    if use_metis:
        netconvert_option2 = os.path.join(data_folder, f"edgesPart{part_idx}.txt")
    else:
        netconvert_option2 = part_bounds[part_idx]

    # Create partition
    print(f"Running netConvert for partition {part_idx}...")
    subprocess.run([
        net_convert_bin,
        netconvert_option1, netconvert_option2,
        "-s", net_file,
        "-o", net_part
    ], check=True)
    print(f"Partition {part_idx} successfully created")

    print(f"Running cutRoutes.py to create routes for partition {part_idx}...")
    # Put output of cut_routes in temp file, edit it, then save it
    cut_routes(cut_routes_options([
        net_part, processed_routes_path,
        "--routes-output", interm_rou_part,
        "--orig-net", net_file,
        "--disconnected-action", "keep"
    ]))
    temp_files.append(interm_rou_part)
    
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
    input_el = cfg_part_el.find("input")
    net_file_el = input_el.find("net-file")
    rou_file_el = input_el.find("route-files")
    gui_file_el = input_el.find("gui-settings-file")

    net_file_el.set("value", net_part)
    rou_file_el.set("value", rou_part)

    if gui_file_el is not None:
        gui_prev_val = gui_file_el.get("value")
        gui_file_el.set("value", os.path.abspath(os.path.join(cfg_dir, gui_prev_val)))

    cfg_part_tree.write(cfg_part)

def _postprocess_partition(
    part_idx: int, min_depart_times: dict,
    data_folder: str = "data",
):
    interm_rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.interm.rou.xml"))
    rou_part = os.path.abspath(os.path.join(data_folder, f"part{part_idx}.rou.xml"))
    
    with open(interm_rou_part, 'r', encoding='utf-8') as fr:
        route_part_tree = ET.parse(fr)
        route_part_el: Element = route_part_tree.getroot()
        vehicles = route_part_el.findall("vehicle")
        remove = []
        for vehicle in vehicles:
            id = vehicle.attrib["id"]
            depart = float(vehicle.attrib["depart"])
            if depart > min_depart_times[id]:
                remove.append(vehicle)
        for el in remove: 
            route_part_el.remove(el)
        route_part_tree.write(rou_part)

def main(args):
    net_convert_bin = os.path.join(os.path.join(SUMO_HOME, 'bin', 'netconvert'))
    partition_network(
        args.num_threads,
        args.net_file,
        args.route_file,
        args.cfg_file,
        net_convert_bin,
        not args.no_metis,
        args.data_folder,
    )

if __name__ == '__main__':
    main(parser.parse_args())