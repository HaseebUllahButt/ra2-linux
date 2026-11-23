#pragma once
#include "ground.h"
#include "../map/map_file.h"
#include <functional>
#include <string>
namespace ra2::sim {
struct ProjectionCellState {
    std::optional<int> level; // Raw signed Cell +11B. No world-floor or sight-height conversion.
    std::optional<bool> flag_1000; // Source Cell +140 bit 1000. Independent from bridge bit 100.
    std::optional<bool> bridge; // Trial Cell +140 bit 100; needed only when source flag 1000 is set.
};
using ProjectionLookup=std::function<std::optional<ProjectionCellState>(ra2map::Cell)>;
enum class CellProjectionStatus { Complete,NeedsInput,Unsupported };
struct CellProjectionResult {
    CellProjectionStatus status=CellProjectionStatus::NeedsInput;
    std::optional<ra2map::Cell> answer;
    ra2map::Cell source_cell; // Diagnostic conversion before Cell lookups.
    unsigned steps=0,lookups=0;
    std::string detail;
};
// Base 006A6A50 / expansion 006D6410. Lookup is read-only and must resolve original
// linear aliases and the fallback Cell. Only Complete supplies a usable answer.
// Input Z is ignored by the original. No Cell flags, Unit positions, or RNG are changed.
CellProjectionResult original_cell_projection(Coord,const ProjectionLookup&);
}
