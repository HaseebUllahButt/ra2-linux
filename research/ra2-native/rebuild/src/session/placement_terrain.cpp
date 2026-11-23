#include "placement_terrain.h"
#include <limits>

namespace ra2::session {
std::optional<PlacementTerrain> PlacementTerrain::create(const PlacementGeometry& geometry,
                                                         const sim::TerrainCells& terrain) {
    if (!placement_cell_in_map({0,0},PlacementEdition::Base,geometry,std::nullopt))
        return std::nullopt;
    const auto count=static_cast<std::size_t>((2*geometry.width-1)*geometry.height);
    if (terrain.size()!=count) return std::nullopt;
    PlacementTerrain result;
    result.geometry_=geometry;
    result.cells_.resize(512*512);
    std::vector<bool> seen(512*512,false);
    for (const auto& cell:terrain) {
        if (cell.cell.x>=512 || cell.cell.y>=512) return std::nullopt;
        const StartPoint point{static_cast<std::int16_t>(cell.cell.x),static_cast<std::int16_t>(cell.cell.y)};
        const auto allocated=placement_cell_in_map(point,PlacementEdition::Base,geometry,std::nullopt);
        if (!allocated || !*allocated) return std::nullopt;
        const auto index=static_cast<std::size_t>(cell.cell.y)*512+cell.cell.x;
        if (seen[index]) return std::nullopt;
        seen[index]=true;
        if (!cell.height_level || !cell.ramp_index) continue;
        if (*cell.height_level < -128 || *cell.height_level > 127 ||
            *cell.ramp_index < 0 || *cell.ramp_index > 255) return std::nullopt;
        result.cells_[index]=PlacementCell{static_cast<std::int8_t>(*cell.height_level),
                                            static_cast<std::uint8_t>(*cell.ramp_index)};
    }
    return result;
}

std::optional<bool> PlacementTerrain::in_map(StartPoint point, PlacementEdition edition) const {
    if (edition==PlacementEdition::Base)
        return placement_cell_in_map(point,edition,geometry_,std::nullopt);
    // 00578460 indexes the grid as y*512+x before testing its bounds. A null
    // pointer uses the default Cell; 0047BBF0 initializes its height and ramp.
    const int index=static_cast<int>(point.y)*512+point.x;
    if (index<0 || index>=512*512)
        return placement_cell_in_map(point,edition,geometry_,PlacementCell{0,0});
    const StartPoint canonical{static_cast<std::int16_t>(index%512),static_cast<std::int16_t>(index/512)};
    const auto allocated=placement_cell_in_map(canonical,PlacementEdition::Base,geometry_,std::nullopt);
    if (!allocated) return std::nullopt;
    if (!*allocated) return placement_cell_in_map(point,edition,geometry_,PlacementCell{0,0});
    return placement_cell_in_map(point,edition,geometry_,cells_[static_cast<std::size_t>(index)]);
}
}
