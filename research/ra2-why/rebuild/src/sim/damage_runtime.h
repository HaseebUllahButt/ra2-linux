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
enum class FlatProjectilePhase { Flying, ImpactPending, RemovalPending };
struct FlatProjectile {
    weapons::ArcInput arc;
    Coord target;
    std::uint32_t source_id;
    FlatProjectilePhase phase=FlatProjectilePhase::Flying;
};
// Advances one existing projectile with the recovered flat Cannon slice.
// An unresolved tick leaves the entire projectile unchanged. ImpactPending
// requires the caller to run original impact effects and damage; it is not a
// completed detonation or removal. RemovalPending requires the original
// +11C(2) and +F0 callbacks for the map-boundary path.
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

namespace ra2::sim {
// Construct only after the original area entry gate has passed and collection
// has completed. Candidate order is fixed at collection time. Recipient state
// is resolved again immediately before each original virtual damage callback.
struct AreaDamageCursor {
    weapons::AreaDamageEvent event;
    int radius;
    std::vector<AreaCandidate> candidates;
    std::size_t next=0;
    std::optional<weapons::DamageRequest> pending;
};
using AreaRecipientLookup=std::function<std::optional<weapons::ResolvedImpactRecipient>(std::uint32_t)>;
enum class AreaDamageStep { Request, Complete, NeedsInput };
struct AreaDamageNext {
    AreaDamageStep status;
    std::optional<weapons::DamageRequest> request;
};
// The pending request repeats until the caller completes the actual derived
// damage callback and confirms it with base_complete_area_damage_request().
// Unknown recipient state never advances the cursor.
AreaDamageNext base_next_area_damage(AreaDamageCursor&, const AreaRecipientLookup&);
bool base_complete_area_damage_request(AreaDamageCursor&,std::uint32_t target_id);
}

namespace ra2::sim {
// Playable prototype only. The exact, guarded APIs above do not use these
// fallbacks. TODO-ORIGINAL: recover attack missions, turret turning, the full
// projectile/infantry handlers, triggers, death effects and deferred deletion.
struct PrototypeCombatProjectile {
    std::uint32_t source_id=0, target_id=0;
    std::string weapon, warhead;
    int damage=0;
    FlatProjectile flight{};
};
struct PrototypeCombatTick {
    std::size_t fired=0, impacts=0, destroyed=0;
    std::vector<std::string> unresolved;
    // Source and target IDs that need an approach route this frame.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> out_of_range;
};
class PrototypeCombatRuntime {
public:
    // The map rules override base rules. All four inputs are copied.
    PrototypeCombatRuntime(const rules::IniFile& base_rules,
        const rules::IniFile& map_rules, const rules::IniFile& art,
        const std::string& game_exe);
    // The Battle mode overrides base rules; the map overrides both.
    PrototypeCombatRuntime(const rules::IniFile& base_rules,const rules::IniFile& mode_rules,
        const rules::IniFile& map_rules,const rules::IniFile& art,const std::string& game_exe);
    // Source and target must be alive, placed Units, Infantry, or Structures.
    // An accepted order remains active until stopped or either object dies.
    bool attack(session::world::Snapshot&,std::uint32_t source_id,std::uint32_t target_id);
    void stop(session::world::Snapshot&,std::uint32_t source_id);
    PrototypeCombatTick tick(session::world::Snapshot&,std::uint64_t frame);
    const std::vector<PrototypeCombatProjectile>& projectiles() const { return projectiles_; }
private:
    rules::IniFile base_,map_,art_;
    OriginalDistance distance_;
    weapons::BaseBallisticAngles angles_;
    std::map<std::uint32_t,std::uint32_t> orders_;
    std::vector<PrototypeCombatProjectile> projectiles_;
};
}
