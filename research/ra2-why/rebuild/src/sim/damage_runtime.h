#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include "../weapons/weapons.h"
#include "prototype_turret.h"
#include <functional>
#include <optional>
#include <set>
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
    bool special_group=false;
};
// Base 0047E5B3..0047E9C8, guarded to flat ground, overlay-free cells,
// Unit and Infantry occupants, and resolved special-group state.
// Returns the original candidate
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
// Base 004F58A0: the victim's House asks its CountryType for an armor
// factor by target RTTI. The country loader 004F9550 reads the five float
// keys from the country section; a missing key or country is 1.0. The
// BuildingType branch (+110/+10C) splits on type BuildCat==5 (Combat).
double country_armor_multiplier(const session::world::House* house,
    session::world::Kind kind, const std::string& type,
    const rules::IniFile& base, const rules::IniFile& map);
// Playable prototype only. The exact, guarded APIs above do not use these
// fallbacks. Turreted Units turn with the original FacingClass and fire only
// within the original facing tolerance; weapons count Burst shots with the
// original burst delays. TODO-ORIGINAL: recover attack missions, the full
// projectile/infantry handlers, triggers, death effects and deferred deletion.
struct PrototypeCombatProjectile {
    std::uint32_t source_id=0, target_id=0;
    std::string weapon, warhead;
    std::uint64_t fired_frame=0;
    int damage=0;
    FlatProjectile flight{};
};
// Value records remain valid after entity death and projectile removal.
enum class PrototypeCombatEventKind { Shot, Damage, Death };
struct PrototypeCombatEvent {
    PrototypeCombatEventKind kind;
    std::uint64_t frame=0, fired_frame=0;
    std::uint32_t source_id=0, target_id=0, source_house=0, target_house=0;
    std::string source_type, target_type, weapon, warhead;
    Coord source_position{}, target_position{};
    int raw_damage=0, hp_before=0, hp_after=0;
};
struct PrototypeCombatTick {
    std::size_t fired=0, impacts=0, destroyed=0;
    std::vector<PrototypeCombatEvent> events;
    std::vector<std::string> unresolved;
    // Source and target IDs that need an approach route this frame.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> out_of_range;
    // Area Guard actors that strayed too far and should drive back to
    // this guard place (original Assign_Destination, virtual +410).
    std::vector<std::pair<std::uint32_t,Coord>> guard_return;
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
    // Uses the same prototype weapon and armor-verse checks as attack().
    // Range, reload timing, and original attack eligibility are not checked.
    // Does not change the world, RNG, attack orders, or projectiles.
    bool can_attack(const session::world::Snapshot&,std::uint32_t source_id,
                    std::uint32_t target_id) const;
    // Source and target must be alive, placed Units, Infantry, Aircraft, or
    // Structures. An accepted order remains active until stopped or either
    // object dies.
    bool attack(session::world::Snapshot&,std::uint32_t source_id,std::uint32_t target_id);
    // The flight mission controls permission to fire. An empty gate keeps
    // the previous behavior. A source the gate rejects skips new shots in
    // the tick idle guard scan and the tick orders loop; it keeps its order.
    // Weapon timing and damage stay shared with the ground prototype. The
    // gate never touches in-flight projectiles or their targets.
    void set_fire_gate(std::function<bool(const session::world::Entity&)> gate) {
        fire_gate_=std::move(gate);
    }
    // The target gate controls permission per source-target pair. An empty
    // gate keeps the previous behavior. A false result blocks can_attack,
    // idle guard acquisition, retaliation and the active orders loop. A
    // rejected active order pauses: it keeps its order, takes no shot and
    // asks no approach. In-flight projectiles are unchanged.
    void set_target_gate(std::function<bool(const session::world::Snapshot&,
        const session::world::Entity&,const session::world::Entity&)> gate) {
        target_gate_=std::move(gate);
    }
    void stop(session::world::Snapshot&,std::uint32_t source_id);
    // Caller-set invulnerability check (base RA2 Iron Curtain only). The
    // callback supplies the current protection state each frame. Base
    // game.exe 006CDC40, specifically 006CDD7B..006CDDDD: virtual+32C
    // protection check, bypass-armor and negative-damage gates, then
    // 0047F600 zero damage return. A target that passes takes no positive
    // damage this frame: its HP and Cell chains stay unchanged and no
    // retaliation starts. Zero or negative damage still lands. This API
    // does not port the 0047F600 visual effect. Checked before the damage
    // kernel.
    using Invulnerability=std::function<bool(std::uint32_t entity,std::uint64_t frame)>;
    void set_invulnerability(Invulnerability check) {invulnerable_=std::move(check);}
    PrototypeCombatTick tick(session::world::Snapshot&,std::uint64_t frame);
    // Base 004E7DF0: kill every remaining owned actor, with no attacker.
    // Shares normal combat death unlinking and emits Death events for callers.
    // Created factory products have no Cell links and are removed too.
    PrototypeCombatTick destroy_house(session::world::Snapshot&,std::uint32_t house,std::uint64_t frame);
    const std::vector<PrototypeCombatProjectile>& projectiles() const { return projectiles_; }
private:
    rules::IniFile base_,map_,art_;
    // Base rules with the map overrides applied. Rank ability and
    // veterancy reads use this so map files change the damage path.
    rules::IniFile merged_;
    OriginalDistance distance_;
    weapons::BaseBallisticAngles angles_;
    std::map<std::uint32_t,std::uint32_t> orders_;
    // Infantry firing animation started after the weapon became ready.
    struct InfantryFire { std::uint32_t target; std::uint64_t fire_frame; };
    std::map<std::uint32_t,InfantryFire> infantry_fire_;
    // Per source-target permission gate. Empty keeps the previous behavior.
    std::function<bool(const session::world::Snapshot&,
        const session::world::Entity&,const session::world::Entity&)> target_gate_;
    Invulnerability invulnerable_; // Caller-set; empty means everyone is vulnerable.
    // Targets found by the idle threat scan or kept after retaliation.
    // Base 006D31E0 Threat_Range: Guard uses GuardRange or the weapon range,
    // Area Guard uses twice GuardRange or the longer weapon range (max 16
    // cells). An Area Guard target stays while it is in that range of the
    // place where the scan found it, so the actor may approach it.
    struct GuardOrder { std::uint32_t target=0; int range=0; Coord anchor{}; bool area=false; };
    std::map<std::uint32_t,GuardOrder> guard_orders_;
    // Original Foot +1B0 archive target: the Area Guard place. It is set by
    // the first Area Guard scan and cleared by attack() and stop().
    std::map<std::uint32_t,Coord> guard_place_;
    std::vector<PrototypeCombatProjectile> projectiles_;
    // Original Unit +2F8 turret FacingClass. The world keeps only the shown
    // raw facing; a different shown facing restarts this state from it.
    // A Unit without a turret never rotates here: the 00704DF0 gate reads
    // the unrotated +2E0 hull facing (FUN_006FB890 turretless branch).
    struct TurretAim { std::uint16_t current, direction; bool turning_before=false; };
    struct TurretState { BaseFacing facing; std::uint16_t written=0; };
    std::map<std::uint32_t,TurretState> turrets_;
    std::set<std::uint32_t> prone_; // Infantry +5C3 after a panic hit.
    // Flight mission fire permission. Empty means every placed source fires.
    std::function<bool(const session::world::Entity&)> fire_gate_;
    std::optional<TurretAim> turret_aim(session::world::Entity& source,
        const session::world::Entity& target,std::int32_t frame,bool omni_fire);
};
}
