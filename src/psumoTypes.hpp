/**
psumoTypes.hpp

Header-only, some shared types in the program.
*/

#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace psumo {
    typedef int partId_t;

    typedef struct border_edge_t {
        std::string id;
        std::vector<std::string> lanes;
        partId_t from;
        partId_t to;
    } border_edge_t;

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(border_edge_t, id, lanes, from, to)
}
