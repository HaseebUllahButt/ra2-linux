#pragma once
#include "../session/world/world.h"
#include "../weapons/weapons.h"
#include <functional>
#include <optional>
#include <string>

namespace ra2::sim {
// Original virtual state not yet present in every loaded world. A false
// collision flag needs no virtual position. Alliance is needed only for the
// selected object when the projectile has a different, non-null source.
struct CollisionProperties {
    std::optional<bool> eligible;
    std::optional<Coord> virtual_position;
    std::optional<bool> allied_to_source;
};
using CollisionLookup = std::function<std::optional<CollisionProperties>(
    const session::world::Entity&, std::uint32_t source_id)>;
struct WorldCollisionResult {
    std::optional<weapons::ProjectileObjectCollision> collision;
    std::string missing;
};
// Base 00471660 / 004608B1. Traverse Cell +E4 and Object +30 directly.
// This function does not change the world, the RNG, or the projectile.
// A missing cell includes the original out-of-map fallback, which is not yet
// represented by Snapshot. No object kind is converted or filtered here.
WorldCollisionResult base_world_projectile_collision(
    const session::world::Snapshot&, std::uint32_t cell_index,
    Coord projectile_position, std::uint32_t source_id, bool preserve_position,
    const CollisionLookup&, const OriginalDistance&);
}

namespace ra2::sim {
struct AreaUnitProperties {
    std::optional<bool> active;
    std::optional<Coord> damage_position; // Original virtual +A0.
    // Original Object +14 bit 1, and virtual +32C when that bit is set.
    std::optional<bool> special_flag, special_virtual;
    // Resolved result of the original scenario's type exclusion list.
    std::optional<bool> excluded_by_scenario;
};
using AreaUnitLookup = std::function<std::optional<AreaUnitProperties>(
    const session::world::Entity&)>;
struct AreaCandidate { std::uint32_t id; int distance; };
struct AreaCollectionResult {
    std::optional<std::vector<AreaCandidate>> candidates;
    std::string missing;
};
// Base 0047E5B3..0047E9C8, guarded to small spreads, flat ground,
// overlay-free cells and Unit occupants. Returns the original candidate
// order, including repeated IDs. It does not dispatch damage. Cells or object
// kinds outside this slice, aerial collection, and special groups block it.
AreaCollectionResult base_small_ground_area_candidates(
    const session::world::Snapshot&, Coord impact, float cell_spread,
    std::uint32_t source_id, bool scenario_type_filter,
    const AreaUnitLookup&, const OriginalDistance&);
}

namespace ra2::sim {
enum class FlatProjectilePhase { Flying, ImpactPending, Removed };
struct FlatProjectile {
    weapons::ArcInput arc;
    Coord target;
    std::uint32_t source_id;
    FlatProjectilePhase phase=FlatProjectilePhase::Flying;
};
// Advances one existing projectile with the recovered flat Cannon slice.
// An unresolved tick leaves the entire projectile unchanged. ImpactPending
// requires the caller to run original impact effects and damage; it is not a
// completed detonation or removal. Removed covers only the map-boundary path.
weapons::FlatCannonTickStatus base_advance_flat_projectile(
    FlatProjectile&, const weapons::FlatCannonTickInput&,
    const weapons::TerrainTraceLookup&, const OriginalDistance&);
}

namespace ra2::sim {
// Base 006D65B0 for a current target whose original Unit locomotor virtual
// +10 reports false. The actor's selected target ID must be resolved by the
// caller. The Unit vtable +58 calls +48, which reads its stored position.
// Moving-target lead, non-Unit targets and unknown locomotor state block it.
std::optional<Coord> base_stationary_unit_aim(
    const session::world::Snapshot&, std::uint32_t source_id,
    std::uint32_t current_target_id, std::optional<bool> target_locomotor_moving);
}
