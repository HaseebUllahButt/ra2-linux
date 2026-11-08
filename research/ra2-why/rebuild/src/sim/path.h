#pragma once
#include "../map/map_file.h"
#include "distance.h"
#include <optional>
#include <string>
#include <vector>
namespace ra2::sim {
// Resolved input domain: equal floor levels, no bridges, tubes, marked terrain, or hierarchy.
// Ordinary Unit profile: type +C94 and +E0C clear, search constructor cost weight one.
// An absent record is an unallocated original cell. An empty status is an unresolved callback.
struct ResolvedFlatPathCell {
    ra2map::Cell cell;
    std::optional<int> entry_status; // Original CanEnterCell result; this slice supports 0 and 7.
};
enum class PathStatus { Found, Partial, NoPath, NeedsInput, Unsupported };
struct PathResult {
    PathStatus status=PathStatus::NoPath;
    std::vector<unsigned> directions;
    ra2map::Cell end;
    float cost=0;
    int expansions=0;
    std::string evidence;
};
// Base 00427370, 00427D40; expansion 00429A90, 0042A460.
// Preserves the original direction order, open-list ties, float storage, and lookup heuristic.
// Nonmonotone route postprocessing remains explicit until 0042B210/0042B7F0 is ported.
PathResult find_original_flat_path(ra2map::Cell source,ra2map::Cell goal,
    const std::vector<ResolvedFlatPathCell>&,const OriginalDistance&,int expansion_limit=-1);
}
