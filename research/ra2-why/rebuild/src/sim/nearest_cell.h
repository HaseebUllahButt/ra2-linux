#pragma once
#include "foot_path.h"
namespace ra2::sim {
struct NearestCellState {
    std::optional<bool> usable; // Base 0055A740 / expansion 00578540 with arg one, including aliases or the fallback Cell.
    std::optional<int> level; // Signed raw Cell +11B, not floor height or the sight-height divisor.
    std::optional<bool> bridge; // Explicit Cell +140 bit 100.
};
struct NearestEmptyCell {
    ra2map::Cell value; // A distinct role from FootPathInputs.invalid_cell. Do not translate between the two sentinels.
};
struct NearestCellInputs {
    FootNearestQuery query; // The resolved fixed Foot call; true Type +AF5/+D2C remains outside this slice.
    std::optional<int> map_width,map_height; // Base Map +F0/+F4, expansion +F4/+F8; raw cell dimensions.
    std::optional<NearestEmptyCell> empty_cell; // Base 00A6F238 / expansion 00ABD480; initializers 005441A0/005618B0 write (0,0).
    std::optional<std::uint32_t> frame; // Needed only when source equals this sentinel.
};
struct NearestFitQuery {
    ra2map::Cell cell;
    int speed_type=0;
    std::uint32_t component=0;
    unsigned movement_zone=0;
    bool above_bridge=false;
    // Original rectangle arguments are width=1, height=1, layer=-1, final flag=0.
};
struct NearestCellCallbacks {
    // Read-only callbacks. Resolve original linear aliases and the fallback Cell before supplying metadata.
    std::function<std::optional<NearestCellState>(ra2map::Cell)> cell;
    std::function<std::optional<bool>(const NearestFitQuery&)> fits; // Base 00550CF0 / expansion 0056E7C0.
    std::function<std::optional<ra2map::Cell>(Coord)> project; // Base 006A6A50 / expansion 006D6410.
};
enum class NearestCellStatus { Complete,NeedsInput,Unsupported };
struct NearestCellResult {
    NearestCellStatus status=NearestCellStatus::NeedsInput;
    std::optional<ra2map::Cell> answer; // A Complete empty answer is the explicit original sentinel, never nullopt.
    std::vector<ra2map::Cell> admitted,exact_projection,other_projection; // Ordered diagnostic lists, including duplicates.
    unsigned probes=0;
    std::string detail;
};
// Base 005502E0 / expansion 0056DC20, with the exact fixed Foot retarget arguments.
// This recovers scan and selection. Rectangle eligibility and projection bodies remain explicit inputs.
NearestCellResult original_foot_nearest_cell(const NearestCellInputs&,const OriginalDistance&,const NearestCellCallbacks&);
}
