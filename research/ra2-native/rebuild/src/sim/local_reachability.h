#pragma once
#include "hierarchy_retry.h"
#include "drive.h"
namespace ra2::sim {
using ReachabilityCell=std::uintptr_t; // Stable caller handle; zero is valid.
struct LocalReachabilityInputs {
    ReachabilityCell source=0;
    unsigned level=0;
    DriveEdition edition=DriveEdition::BaseRa2;
    std::optional<unsigned> movement_zone;
    std::vector<std::uint16_t> initial_regions;
};
struct LocalReachabilityCallbacks {
    std::function<std::optional<ra2map::Cell>(ReachabilityCell)> coordinates;
    std::function<std::optional<std::uint16_t>(ReachabilityCell,unsigned)> region;
    std::function<std::optional<ReachabilityCell>(ReachabilityCell,unsigned)> neighbor;
    std::function<std::optional<std::int8_t>(ReachabilityCell)> height;
    // Original CanEnterCell(neighbor,direction,signed height,0,1).
    std::function<std::optional<int>(ReachabilityCell,unsigned,std::int8_t)> entry;
    std::function<std::optional<unsigned>(ReachabilityCell)> category; // Cell +4C, not LandType.
    std::function<std::optional<bool>(ra2map::Cell)> usable; // Original query with boolean argument true.
    std::function<std::optional<std::uint16_t>(ra2map::Cell,unsigned)> scan_region;
    std::function<std::optional<std::vector<std::uint16_t>>(std::uint16_t,unsigned)> links;
};
struct LocalReachabilityResult {
    RetryEdgesStatus status=RetryEdgesStatus::NeedsInput;
    bool reachable=false; // Original true result; inspect only when status is Complete.
    std::vector<std::uint16_t> regions;
    std::array<bool,64> visited{}; // Original stride eight. Only the size-by-size corner is original state.
    std::string detail;
};
// Base 00565EB0 / expansion 005840C0. Resolves local flood order, the exact
// repeated-coordinate scan, and reverse output-link order. Called bodies stay explicit.
LocalReachabilityResult original_local_reachability(const LocalReachabilityInputs&,
    const LocalReachabilityCallbacks&);
}
