#pragma once
#include "session.h"
#include "../sim/world.h"
#include <memory>

namespace ra2::session {
// Snapshot of a complete original cell grid. Missing height or ramp data stays
// unresolved. Build it from the same terrain pass used by placement.
class PlacementTerrain {
public:
    static std::optional<PlacementTerrain> create(const PlacementGeometry&, const sim::TerrainCells&);
    std::optional<bool> in_map(StartPoint, PlacementEdition) const;
private:
    PlacementGeometry geometry_;
    std::vector<std::optional<PlacementCell>> cells_;
};
}
