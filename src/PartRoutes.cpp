// C++ version of partRoutes.py, 
// tested less

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <thread>

#include "libs/tinyxml2.h"
#include "libs/argparse.hpp"

using namespace tinyxml2;
using namespace std;

const string ROUTE = "route";
const string VEHICLE = "vehicle";
const string PERSON = "person";
const string TRIP = "trip";
const string CONTAINER = "container";
const string FLOW = "flow";
const string PERSON_FLOW = "personFlow";
const string CONTAINER_FLOW = "containerFlow";
const string INTERVAL = "interval";
const string INCLUDE = "include";
const string VTYPE = "vType";
const string ROUTE_DISTRIBUTION = "routeDistribution";
const string VTYPE_DISTRIBUTION = "vTypeDistribution";

vector<string> route_owners = {VEHICLE, PERSON, FLOW, PERSON_FLOW};

vector<string> keep_tags = {VTYPE, VTYPE_DISTRIBUTION, INTERVAL};

vector<string> unhandled_tags = {ROUTE_DISTRIBUTION, CONTAINER, CONTAINER_FLOW, INCLUDE};

bool route_node_is_first(XMLElement* route_node) {
    const char* id = route_node->Attribute("id");
    return (id && (strstr(id, "part0") != nullptr || strstr(id, "part") == nullptr));
}

vector<XMLElement*> filter_or_split_route(XMLElement* route, vector<string>& partEdgeIds, bool keepMultipart = false) {
    // Output
    vector<XMLElement*> routeParts;

    vector<vector<string>> routePartsEdges;
    vector<string> currentPart;

    // iterate over "words" separate by spaces
    istringstream edgeStream(route->Attribute("edges"));
    string edge, firstEdge;
    bool assignedFirstEdge = false;

    while (edgeStream >> edge) {
        if (!assignedFirstEdge) {
            assignedFirstEdge = true;
            firstEdge = edge;
        }
        auto it = find(partEdgeIds.begin(), partEdgeIds.end(), edge);
        if (it != partEdgeIds.end()) {
            currentPart.push_back(edge);
        } else if (!currentPart.empty()) {
            routePartsEdges.push_back(currentPart);
            currentPart.clear();
        }
    }

    if (!currentPart.empty()) {
        routePartsEdges.push_back(currentPart);
    }

    if (routePartsEdges.empty()) {
        return routeParts;
    // Normal, non-multipart route
    } else if (routePartsEdges.size() == 1) {
        stringstream outEdges;
        for (int i = 0; i < routePartsEdges[0].size(); i++) {
            outEdges << routePartsEdges[0][i];
            if (i < routePartsEdges[0].size() - 1) outEdges << " ";
        }
        route->SetAttribute("edges", outEdges.str().c_str());
        route->SetAttribute("id_og", route->Attribute("id"));
        if (routePartsEdges[0][0] == firstEdge) {
            route->SetAttribute("is_start", "true");
        }
        routeParts.push_back(route);
        return routeParts;
    } else if (!keepMultipart) {
        return routeParts;
    } else {
        routeParts.reserve(routePartsEdges.size());
        const char* id = route->Attribute("id");
        int digits = to_string(routePartsEdges.size()).size();

        for (int i = 0; i < routePartsEdges.size(); i++) {
            XMLElement* part = static_cast<XMLElement*>(route->ShallowClone(nullptr));
            string iStr = to_string(i);
            string iPrefix = string(digits - iStr.size(), '0') + iStr;
            part->SetAttribute("id", (id + string("_part") + iPrefix).c_str());
            part->SetAttribute("id_og", id);

            stringstream outEdges;
            for (int i = 0; i < routePartsEdges[0].size(); i++) {
                outEdges << routePartsEdges[0][i];
                if (i < routePartsEdges[0].size() - 1) outEdges << " ";
            }

            part->SetAttribute("edges", outEdges.str().c_str());
            if (routePartsEdges[i][0] == firstEdge) {
                part->SetAttribute("is_start", "true");
            }
            routeParts.push_back(part);
        }
        return routeParts;
    }
}

void part_route(
    const string& routesFile, const string& partitionNetworkFile, 
    const string& outputRouteFile, bool splitInterruptedRoutes = true
) {
    XMLDocument routesTree;
    if (routesTree.LoadFile(routesFile.c_str()) != XML_SUCCESS) {
        cerr << "Failed to load routes file." << endl;
        return;
    }

    XMLElement* routesRoot = routesTree.RootElement();
    XMLDocument output_root;

    XMLDocument net;
    if (net.LoadFile(partitionNetworkFile.c_str()) != XML_SUCCESS) {
        cerr << "Failed to load partitioned network file." << endl;
        return;
    }

    // ... rest of the code ...

    output_root.InsertEndChild(new_routes[i]);
    // ...

    if (output_root.SaveFile(outputRouteFile.c_str()) != XML_SUCCESS) {
        cerr << "Failed to write output route file." << endl;
    }
}

int main(int argc, char* argv[]) {
    argparse::ArgumentParser parser("partroutes", "1.0");
    parser.add_argument("-r", "--routes").required().help("Routes input file");
    parser.add_argument("-n", "--network").required().help("Partition input file");
    parser.add_argument("-o", "--out").required().help("Output partitioned route file");

    parser.parse_args(argc, argv);

    part_route(
        parser.get<string>("routes"), 
        parser.get<string>("network"), 
        parser.get<string>("out")
    );

    return 0;
}
