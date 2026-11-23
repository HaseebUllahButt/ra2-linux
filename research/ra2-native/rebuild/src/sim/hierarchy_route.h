#pragma once
#include "hierarchy.h"
#include "drive.h"
namespace ra2::sim {
struct HierarchyRouteInputs {
    std::size_t source_index=0,destination_index=0;
    unsigned movement_zone=0;
    DriveEdition edition=DriveEdition::BaseRa2;
    std::array<std::vector<std::pair<std::uint16_t,std::uint16_t>>,3> blocked_edges;
};
struct HierarchyRouteLayer {
    std::vector<std::uint16_t> path;
    std::vector<bool> allowed;
    std::vector<std::optional<float>> best_costs;
};
struct HierarchyRoute {
    bool found=false;
    std::array<HierarchyRouteLayer,3> layers;
};
// Base 00429B60 / expansion 0042C290 with no threat contribution.
// The graph must come from resolved original map state. This is not the Foot goal wrapper.
HierarchyRoute find_original_hierarchy_route(const Hierarchy&,const HierarchyRouteInputs&);
}
