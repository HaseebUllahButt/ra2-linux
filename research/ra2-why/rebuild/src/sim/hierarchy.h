#pragma once
#include "regions.h"
#include <array>
namespace ra2::sim {
struct HierarchyCell {
    CoarseRegionCell coarse;
    std::optional<bool> usable; // Resolved original usable-area query with its boolean argument set.
};
struct HierarchyLink { std::uint16_t node; bool diagonal; };
struct HierarchyNode {
    std::uint16_t parent=0;
    std::uint8_t category=7;
    std::optional<int> center_index; // Dummy node zero has no recovered center value.
    std::vector<HierarchyLink> links;
};
struct HierarchyLayer {
    std::vector<std::uint16_t> labels;
    std::vector<HierarchyNode> nodes;
};
struct Hierarchy {
    CoarseRegions coarse;
    std::array<HierarchyLayer,3> layers; // Original levels zero, one, two; built in reverse order.
};
// Base 00563D80/00564290; expansion 00581F90/005824A0.
// This slice excludes tube/bridge transition records and requires resolved coarse categories.
Hierarchy build_original_hierarchy(unsigned stride,const std::vector<HierarchyCell>&);
}
