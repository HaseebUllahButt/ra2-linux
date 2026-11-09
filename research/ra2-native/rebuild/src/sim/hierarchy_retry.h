#pragma once
#include "retry_edges.h"
#include "../map/map_file.h"
#include <functional>
namespace ra2::sim {
struct HierarchyRetryState {
    bool allow_hierarchy_38=true;
    std::array<std::vector<std::pair<std::uint16_t,std::uint16_t>>,3> blocked;
};
struct HierarchyRetryInputs {
    // Search +70 is the last progress cell recorded by the regular search.
    // It is not necessarily the search endpoint or the requested goal.
    std::optional<ra2map::Cell> progress_cell;
    std::array<std::vector<std::uint16_t>,3> paths;
};
struct RetryReachability {
    bool reachable=false;
    std::vector<std::uint16_t> regions; // Original output order; duplicates stay explicit.
};
using RetryRegionEntry=std::uintptr_t; // Opaque caller handle; zero is valid.
struct HierarchyRetryCallbacks {
    // Resolve 0054FAB0 / 0056D3F0 plus its table lookup to a stable entry.
    // Keep the table identity as well as its index if a callback replaces tables.
    std::function<std::optional<RetryRegionEntry>(ra2map::Cell)> region_entry;
    std::function<std::optional<std::uint16_t>(RetryRegionEntry,unsigned)> region;
    // Includes original cell lookup and 00565EB0 / 005840C0. This body is not supplied here.
    std::function<std::optional<RetryReachability>(ra2map::Cell,unsigned)> reachability;
};
enum class HierarchyRetryStep { RegionEntry, Reachability };
struct HierarchyRetryEvent { HierarchyRetryStep step; ra2map::Cell cell; unsigned level=0; };
struct HierarchyRetryResult {
    RetryEdgesStatus status=RetryEdgesStatus::NeedsInput;
    HierarchyRetryState state;
    std::vector<HierarchyRetryEvent> events;
    std::string detail;
};
// Base 0042A590 / expansion 0042CCD0. All three levels run even after +38 clears.
// Callbacks operate on private search state and can change progress or region entries.
// NeedsInput retains completed earlier levels in the result. It does not publish them.
HierarchyRetryResult run_original_hierarchy_retry(const Hierarchy&,
    const HierarchyRetryInputs&,const HierarchyRetryState&,const HierarchyRetryCallbacks&);
}
