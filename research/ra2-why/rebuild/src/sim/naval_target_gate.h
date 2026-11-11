#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>

namespace ra2::sim {
// Base 006C95B0..006C9630 rejects land when LandTargeting=1. Cell virtual
// +50 (0047B9F0) treats the 14 tiles at General WaterSet as water. A ground
// object on LandType Water or Beach also passes, unless it is on a bridge.
// Unknown map data stays permissive until the missing Cell state is resolved.
inline bool naval_target_allowed(const rules::IniFile& rules,
    const session::world::Snapshot& world, const session::world::Entity& source,
    const session::world::Entity& target, std::int32_t water_first) {
    if (rules.read_integer(source.type,"LandTargeting",0)!=1 || !target.cell) return true;
    const auto key=std::uint32_t(target.cell->y)*512u+target.cell->x;
    const auto found=world.cells.find(key);
    if (found==world.cells.end() || !found->second.terrain) return true;
    const auto& cell=*found->second.terrain;
    if (!cell.land_type || !cell.resolved_tile_index || water_first<0) return true;
    const auto tile=*cell.resolved_tile_index;
    if (tile>=static_cast<std::uint32_t>(water_first) &&
        std::uint64_t(tile)<std::uint64_t(water_first)+14) return true;
    if (*cell.land_type==2 || *cell.land_type==6) {
        if (!target.layer) return true;
        if (*target.layer==session::world::Layer::Bridge) return false;
        if (!target.position || !cell.floor_world_height) return true;
        if (std::int64_t(target.position->z)-*cell.floor_world_height<208) return true;
    }
    return false;
}
}
