"""
Split or filter the routes in a SUMO routes xml so that they are contained
within a SUMO network xml partitioned within its original network, possibly
splitting a route into more parts in case a inbetween part goes out of the
partition.

Similar to cutRoutes.py from SUMO tools, but with some differences in the
output to be used in Parallel-Sumo.
"""

import os, sys
import argparse
# use ElementTree as SUMO lib doesn't have a specialized
# rou.xml reader, so prefer the more used library
# lxml for better XPath
import lxml.etree
from lxml.etree import _Element as Element, _ElementTree as ElementTree
from multiprocessing.pool import ThreadPool
import copy
import itertools

if 'SUMO_HOME' in os.environ:
    SUMO_HOME = os.environ['SUMO_HOME']
    sumo_tools = os.path.join(SUMO_HOME, 'tools')
    sys.path.append(os.path.join(sumo_tools))

    import sumolib
    from sumolib.net import Net
    from sumolib.net.edge import Edge
else:
    sys.exit("please declare environment variable 'SUMO_HOME'")

parser = argparse.ArgumentParser()
parser.add_argument("-r", "--routes", required=True, type=str, help="Routes input file")
parser.add_argument("-n", "--network", required=True, type=str, help="Partition input file")
parser.add_argument("-o", "--out", required=True, type=str, help="Output partitioned route file")

# from demand xml schema http://sumo.dlr.de/xsd/routes_file.xsd
# from route xml schema http://sumo.dlr.de/xsd/routeTypes.xsd
# for info https://sumo.dlr.de/docs/Definition_of_Vehicles%2C_Vehicle_Types%2C_and_Routes.html
_ROUTE = "route"
_VEHICLE = "vehicle"
_PERSON = "person"
_TRIP = "trip"
# https://sumo.dlr.de/docs/Specification/Containers.html
_CONTAINER = "container"
_FLOW = "flow"
_PERSON_FLOW = "personFlow"
_CONTAINER_FLOW = "containerFlow"
_INTERVAL = "interval"
_INCLUDE = "include"
_VTYPE = "vType"
_ROUTE_DISTRIBUTION = "routeDistribution"
_VTYPE_DISTRIBUTION = "vTypeDistribution"

# Elements with a route, to keep if their route
# starts in this partition
route_owners = [
    _VEHICLE,
    _PERSON,
    _FLOW,
    _PERSON_FLOW,
]

# Tag to keep the same
keep_tags = [
    _VTYPE,
    _VTYPE_DISTRIBUTION,
    _INTERVAL,
]

# Drop with warning, may implement later
unhandled_tags = [
    _ROUTE_DISTRIBUTION,
    _CONTAINER,
    _CONTAINER_FLOW,
    _INCLUDE,
]

def _route_node_is_first(route_node: Element) -> bool:
    id: str = route_node.attrib["id"]
    return id.endswith("_part0") or "_part" not in id

def part_route(
    routes_file: str,
    partition_network_file: str,
    output_route_file: str | None = None,
    split_interrupted_routes = True,
):
    """Split or filter the routes in a SUMO routes xml so that they are contained
    within a SUMO network xml partitioned within its original network, possibly
    splitting a route into more parts in case a inbetween part goes out of the
    partition.

    Args:
        routes_file (str): Input routes file path. Note that trips are not supported, so routes must be converted
            with duarouter before in case to turn them into routes.
        partition_network_file (str): Input partitioned network file path (network of the single partition)
        output_route_file (str | None, optional): Path to write the route xml to, set to None to return the xml tree. Defaults to None.
        split_interrupted_routes (boolean): Keep routes that are split in the middle by going out of the partition, 
            or remove them.
    """
    
    routes_tree: ElementTree = lxml.etree.parse(routes_file)
    routes_root: Element = routes_tree.getroot()
    output_root: Element = lxml.etree.Element("routes")
    
    net: Net = sumolib.net.readNet(partition_network_file)
    
    # Error if route contains trips or flows with no route
    # Note that the flow tag can both be used for flows with
    # a precalculated route ("route" attr) and with trip-like
    # "from" and "to" tags that only define beginning and end
    # edges
    
    if (
        routes_root.xpath(f".//{_TRIP}") 
        or routes_root.xpath(f".//{_FLOW}[not(@route)]")
    ):
        raise ValueError("Won't handle trip or flow (without route) tags! Convert them using SUMO duarouter first.")
        
    routes = routes_tree.findall(_ROUTE)
    with ThreadPool(4) as pool:
        edge_ids = pool.map(lambda e: e.getID(), net.getEdges())
        new_routes = list(itertools.chain(*pool.map(lambda route: _filter_or_split_route(route, edge_ids, split_interrupted_routes), routes)))
    # routes_by_id = {route.attrib["id"]: route for route in new_routes}
    routes_first_parts_by_id = {route.attrib["id_og"]: route for route in new_routes if _route_node_is_first(route)}
                
    output_root.extend(new_routes)
    
    unhandled_tags_copy = unhandled_tags.copy()
    for child in routes_root:
        child: Element = child
        if child.tag in keep_tags:
            output_root.append(child)
        elif child.tag in route_owners:
            if child.find("route"):
                raise ValueError("Nested routes inside vehicles or other are not supported!")
            route_id = child.attrib["route"]
            if route_id in routes_first_parts_by_id:
                route = routes_first_parts_by_id[route_id]
                if "is_start" in route.attrib:
                    # set to proper first id in multipart routes
                    child.set("route", route.attrib["id"])
                    output_root.append(child)

        elif child.tag in unhandled_tags_copy:
            print(f"[WARN] Removed {child.tag} element(s) as it is not supported yet", file=sys.stderr)
            unhandled_tags_copy.remove(child.tag)
            
    out_tree: ElementTree = lxml.etree.ElementTree(output_root)
    out_tree.write(output_route_file)

__EMPTY = []

# Note that route can get modified
def _filter_or_split_route(route: Element, part_edge_ids: list, keep_multipart = False)->list[Element]:
    edges: list[str] = route.attrib["edges"].split()
    
    route_parts = []
    current_part = []

    for edge in edges:
        if edge in part_edge_ids:
            current_part.append(edge)
        elif current_part:
            route_parts.append(current_part.copy())
            current_part = []
    if current_part:
        route_parts.append(current_part)
        
    if len(route_parts) == 0:
        return __EMPTY
    elif len(route_parts) == 1:
        route.set("edges", ' '.join(route_parts[0]))
        route.set("id_og", route.attrib["id"])
        if (route_parts[0][0] == edges[0]):
            route.set("is_start", "true")
        return [route]
    elif not keep_multipart:
        return __EMPTY
    else:
        parts = [ copy.copy(route) for _ in route_parts ]
        id = route.attrib["id"]
        
        # Make sure each number for the same route has the same amount of digits, 
        # so sorting in route processing works well
        digits = len(str(len(parts)))
        
        for (i, part), part_edges in zip(enumerate(parts), route_parts):
            i_str = str(i)
            i_prefix = ('0'*(digits-len(i_str)))+i_str
            part.set("id", f"{id}_part{i_prefix}")
            part.set("id_og", id)
            part.set("edges", ' '.join(part_edges))
            if part_edges[0] == edges[0]:
                part.set("is_start", "true")
        return parts
            
    
if __name__ == '__main__':
    args = parser.parse_args()
    part_route(args.routes, args.network, args.out)