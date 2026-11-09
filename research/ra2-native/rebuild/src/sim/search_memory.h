#pragma once
#include "../map/map_file.h"
#include <array>
#include <cstdint>
namespace ra2::sim {
struct HierarchyRoute;
// Search constructor: base 00427FB0 / expansion 0042A6D0.
// These defaults are original constructor writes, not unresolved imported state.
struct OriginalSearchMemory {
    std::array<std::array<std::uint16_t,500>,3> paths{}; // Search +BC, three contiguous 1000-byte buffers.
    std::array<std::uint32_t,3> counts{}; // +C74.
    std::int32_t progress_6c=-1;
    ra2map::Cell progress_cell_70{0,0};
};
// Base 00429B60 / expansion 0042C290 writes completed levels from two to zero.
// A failed lower level leaves its prior count and words intact. Prefix writes
// never clear the remaining tail. The ordinary search reset preserves all fields here.
void retain_original_hierarchy_paths(OriginalSearchMemory&,const HierarchyRoute&);
}
