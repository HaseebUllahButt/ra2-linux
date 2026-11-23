#pragma once
// Prototype special unit rules for the first playable native Linux skirmish:
// infantry garrison, engineer capture and bridge repair, spy infiltration,
// veterancy from kills, and crate bonuses. Everything reads the loaded base
// RULES.INI and the shared session world snapshot. This is a playable
// simplification, not the original RA2 mission and combat code.
//
// Original anchors recovered from the decompiled base game.exe:
// - BuildingClass::CanBeOccupiedBy 00457CE0 (CanBeOccupied +0x157B,
//   MaxNumberOccupants +0x1580 on the building type).
// - FootClass garrison 004DFE00 and building entry 00522910 (Occupier
//   infantry move into the building occupant vector).
// - Building infiltration effects 004571E0 (power outage, radar reshroud,
//   superweapon reset, cash steal, lab tech unlock).
// - VeterancyStruct::Add 0074FF50 (victimCost / (ownerCost * VeteranRatio),
//   clamp VeteranCap) with rank checks 0074FF90/00750010.
// - CellClass::CollectCrate 00476CC0 crate pickup (overlay data selects the bonus).
// The RA1 ancestor shows the engineer and spy shapes in
// ref/CnC_Red_Alert/CODE/INFANTRY.CPP Per_Cell_Process.
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {

// ---------------------------------------------------------------------------
// Veterancy
// ---------------------------------------------------------------------------

// Original rank thresholds: veteran at 1.0, elite at 2.0 (0074FF90/00750010).
enum class Rank { Rookie, Veteran, Elite };

// The 18 original veteran ability flags, in AbilitiesStruct order
// (Phobos-YRpp TechnoTypeClass.h; TSpp tibsun_defines.h AbilityType).
enum Ability {
    AbilityFaster = 0, AbilityStronger, AbilityFirepower, AbilityScatter,
    AbilityRof, AbilitySight, AbilityCloak, AbilityTiberiumProof,
    AbilityVeinProof, AbilitySelfHeal, AbilityExplodes, AbilityRadarInvisible,
    AbilitySensors, AbilityFearless, AbilityC4, AbilityTiberiumHeal,
    AbilityGuardArea, AbilityCrusher, AbilityCount = 18
};

// [General] veteran factors. RA2 has no elite multipliers; elite reuses the
// veteran values, gated by the EliteAbilities flags.
struct VeterancyRules {
    double veteran_ratio = 3.0;  // victim value multiple per veterancy level.
    double veteran_combat = 1.1; // firepower multiplier.
    double veteran_speed = 1.2;  // speed multiplier.
    double veteran_sight = 0.0;  // sight multiplier, 0 means no change.
    double veteran_armor = 1.5;  // incoming damage divisor.
    double veteran_rof = 0.6;    // weapon delay multiplier.
    double veteran_cap = 2.0;    // maximum veterancy value.
    bool initial_veteran = false;
    // Reads [General]; throws when a value is present but invalid.
    static VeterancyRules load(const rules::IniFile& rules);
};

struct Abilities { bool flag[AbilityCount] = {}; };
struct TypeAbilities { Abilities veteran, elite; };

// Reads VeteranAbilities= and EliteAbilities= of one type. Unknown ability
// names throw. Absent keys leave all flags false.
TypeAbilities load_type_abilities(const rules::IniFile& rules, std::string_view type);

Rank rank_of(double veterancy) noexcept;

// True when the type may gain veterancy. Base defaults: units, infantry and
// aircraft are trainable (TechnoTypeClass ctor 00710AF0 sets +0xC8E), buildings
// are not (BuildingTypeClass ctor 0045DD90). The Trainable= INI key overrides.
bool trainable(const rules::IniFile& rules, std::string_view type,
    session::world::Kind kind);

struct Promotion {
    bool promoted = false;
    Rank before = Rank::Rookie, after = Rank::Rookie;
    double veterancy = 0.0;
};

// Adds victimCost / (killerCost * VeteranRatio) to the killer's veterancy,
// clamped to VeteranCap (0074FF50). Both entities must be live, the killer
// must be trainable, and the houses must differ. Costs come from the type
// rules. Throws std::invalid_argument for unknown or dead entities.
// TODO-ORIGINAL 006297F0: the original also gates on the victim being
// trainable and skips allied kills; garrisoned buildings count occupant
// value instead of the building value (00708B40).
Promotion record_kill(const rules::IniFile& rules, const VeterancyRules& general,
    session::world::Snapshot& world, std::uint32_t killer_id, std::uint32_t victim_id);

// Ability-gated combat multipliers. The FIREPOWER/STRONGER/ROF/FASTER/SIGHT
// ability gates apply; an elite unit keeps the same [General] values.
double veteran_firepower_multiplier(const TypeAbilities&, Rank,
    const VeterancyRules&) noexcept;
double veteran_armor_divisor(const TypeAbilities&, Rank, const VeterancyRules&) noexcept;
double veteran_rof_multiplier(const TypeAbilities&, Rank, const VeterancyRules&) noexcept;
double veteran_speed_multiplier(const TypeAbilities&, Rank, const VeterancyRules&) noexcept;
double veteran_sight_multiplier(const TypeAbilities&, Rank, const VeterancyRules&) noexcept;

// ---------------------------------------------------------------------------
// Garrison
// ---------------------------------------------------------------------------

// Building type garrison rules: CanBeOccupied / CanOccupyFire /
// MaxNumberOccupants (BuildingTypeClass +0x157B/+0x157C/+0x1580).
struct GarrisonRules {
    bool can_be_occupied = false, can_occupy_fire = false;
    std::int32_t max_occupants = 0;
    static GarrisonRules load(const rules::IniFile& rules, std::string_view type);
};

// Per-match garrison state. The world snapshot has no occupant vector, so the
// caller owns one GarrisonState for the match.
class GarrisonState {
public:
    // Moves a live Occupier infantry of the owning (or allied) house into a
    // live CanBeOccupied building with room left. The infantry leaves the map
    // (in_limbo) and keeps the building cell. Returns false without changes
    // when any check fails.
    bool garrison(const rules::IniFile& rules, session::world::Snapshot& world,
        std::uint32_t building_id, std::uint32_t infantry_id);
    // All occupants leave the building at the building cell. Returns the
    // infantry ids in garrison order. TODO-ORIGINAL 00481180: the original
    // scatters leavers to free infantry subpositions around the building.
    std::vector<std::uint32_t> ungarrison(session::world::Snapshot& world,
        std::uint32_t building_id);
    std::vector<std::uint32_t> occupants(std::uint32_t building_id) const;
    std::size_t occupant_count(std::uint32_t building_id) const;
    // Summed Primary weapon Damage of all live occupants. Zero when the
    // building type has no CanOccupyFire. TODO-ORIGINAL: the original fires
    // each occupant weapon on its own ROF from the building muzzle.
    std::int32_t occupant_fire_damage(const session::world::Snapshot& world,
        const rules::IniFile& rules, std::uint32_t building_id) const;
    // Drops destroyed or lost entities from the state; returns true when
    // anything was dropped.
    bool purge_destroyed(const session::world::Snapshot& world);
private:
    std::map<std::uint32_t, std::vector<std::uint32_t>> occupants_;
};

// ---------------------------------------------------------------------------
// Engineer
// ---------------------------------------------------------------------------

enum class EngineerEffect { None, RepairedAlly, Captured, Damaged, BridgeRepaired };
struct EngineerResult {
    EngineerEffect effect = EngineerEffect::None;
    std::int32_t damage = 0; // Only for Damaged.
};

// An Engineer infantry entering a building (RA1 INFANTRY.CPP 591-640):
// friendly building: full repair. Enemy building: capture when the health
// ratio is at or below engineer_capture_level, else damage it. The engineer is
// consumed in every case. TODO-ORIGINAL: the RA2 consumer of
// EngineerCaptureLevel is not recovered; the default 0.25 follows the RA1
// compiled default (Rule.ConditionRed, RULES.H 286). The damage step follows
// the RA1 MaxStrength/3 shape.
EngineerResult engineer_enter_building(const rules::IniFile& rules,
    session::world::Snapshot& world, std::uint32_t engineer_id,
    std::uint32_t building_id, double engineer_capture_level = 0.25);

// An Engineer infantry standing on or next to a broken bridge cell repairs it.
// broken_bridges maps packed cell index (y*512+x) to broken state. The cell
// becomes usable again and the engineer is consumed. Returns false when the
// engineer is not a live Engineer, the cell is not broken, or the engineer is
// not on or adjacent to the cell. TODO-ORIGINAL 00571490: the original
// retiles the connected bridge run; this prototype only clears the marker.
bool engineer_repair_bridge(session::world::Snapshot& world,
    std::map<std::uint32_t, bool>& broken_bridges, std::uint32_t engineer_id,
    std::uint32_t cell_index);

// ---------------------------------------------------------------------------
// Spy
// ---------------------------------------------------------------------------

enum class SpyEffectKind {
    None, PowerOutage, RadarReset, MoneyStolen, SuperweaponReset, TechUnlock
};
struct SpyEffect {
    SpyEffectKind kind = SpyEffectKind::None;
    std::int32_t amount = 0; // Credits for MoneyStolen.
};

// Per-match infiltration effects with durations. The caller applies the power
// and radar effects to its own house models.
struct SpyInfiltrations {
    std::map<std::uint32_t, std::int32_t> power_outage_frames;  // house -> remaining.
    std::map<std::uint32_t, std::int32_t> radar_off_frames;     // house -> remaining.
    std::map<std::uint32_t, std::vector<std::string>> unlocked_labs; // house -> lab types.
    // Counts every timer down; removes exhausted entries.
    void tick(std::int32_t frames);
};

// A spy (Infiltrate=yes) entering an enemy building applies one effect and is
// consumed (004571E0). Building type decides the effect, first match wins:
// - Power > 0: power outage on the owning house, SpyPowerBlackout frames.
// - Radar=yes: radar reset on the owning house (outage frames; the original
//   also reshrouds their explored map, not modeled here).
// - SuperWeapon= set: superweapon reset on the owning house.
// - Type in lab_types: tech unlock for the spy's house (lab index order).
// - ConstructionYard=yes: cash steal, SpyMoneyStealPercent of the victim cash.
// Throws std::invalid_argument for unknown or dead entities, a non-spy, or an
// own-house building. TODO-ORIGINAL 004571E0: the original lab list lives at
// Rules +0x920 and the exact per-type branches are only partly recovered.
SpyEffect spy_infiltrate(const rules::IniFile& rules, session::world::Snapshot& world,
    SpyInfiltrations& state, std::uint32_t spy_id, std::uint32_t building_id,
    double spy_power_blackout_frames = 1000.0, double spy_money_steal_percent = 0.5,
    const std::vector<std::string>& lab_types = {"GATECH", "NATECH"});

// ---------------------------------------------------------------------------
// Crates
// ---------------------------------------------------------------------------

// Playable subset of the original crate powerups (CellClass::CollectCrate
// 00476CC0 crate pickup). Types the prototype cannot execute (squad, shroud, sonar,
// para bomb, ICBM, time quake, invulnerability, vortex, tiberium, poison)
// fall back to money, as the original falls back when a powerup has no
// effect. TODO-ORIGINAL: the original type order and share weights are
// runtime tables; the defaults here follow the RA1 CONST.CPP shares.
enum class CrateKind {
    Money = 0, Unit, HealBase, Armor, Speed, Firepower, Veterancy, Cloak,
    Explosion, CrateKindCount
};

struct CrateRules {
    std::int32_t share[static_cast<std::size_t>(CrateKind::CrateKindCount)] = {};
    double radius = 3.0;        // [CrateRules] CrateRadius, cells.
    std::int32_t crate_maximum = 255, crate_minimum = 1;
    std::int32_t solo_money = 5000; // [CrateRules] SoloCrateMoney.
    double regen_minutes = 3.0;     // [CrateRules] CrateRegen.
    std::string unit_crate_type;    // [CrateRules] UnitCrateType, empty = money.
    // Bonus multipliers, loaded from [Powerups] fourth fields.
    double armor_crate = 1.5, speed_crate = 1.2, firepower_crate = 2.0;
    bool free_mcv = true;           // [CrateRules] FreeMCV.
    // Reads [CrateRules]; unknown bonus names throw. Absent keys keep the
    // defaults. The unit share covers Unit; the other original types fall
    // back to money.
    static CrateRules load(const rules::IniFile& rules);
};

struct Crate {
    CrateKind kind = CrateKind::Money;
    std::uint32_t expire_frame = 0; // Spawn frame plus regen time.
};

// Per-match crate state keyed by packed cell index (y*512+x).
class CrateState {
public:
    // Weighted random pick from the shares, using the shared world random
    // state (drawn and written back). Does not validate the cell.
    CrateKind random_kind(session::world::Snapshot& world, const CrateRules& rules) const;
    // Places one crate of the given kind. Returns false when the cell already
    // holds a crate.
    bool place(std::uint32_t cell_index, CrateKind kind, std::uint32_t now_frame,
        const CrateRules& rules);
    // Removes expired crates; returns their cell indexes.
    std::vector<std::uint32_t> expire(std::uint32_t now_frame);
    const Crate* find(std::uint32_t cell_index) const;
    std::size_t size() const noexcept { return crates_.size(); }
    // True when the house has more live units than the original unit crate
    // limit (50, 00476CC0 crate pickup case 1).
    static bool unit_limit_reached(const session::world::Snapshot& world,
        std::uint32_t house_id);
    // Applies the crate bonus at the cell to the collector. The collector must
    // be a live entity of a known house. Money adds credits to the house.
    // HealBase heals all live entities of the collector's house. Armor, Speed
    // and Firepower multiply the matching bias of every live techno within
    // CrateRadius cells (only when the bias is still 1.0). Veterancy promotes
    // trainable technos in radius by one rank. Cloak marks technos in radius
    // cloakable. Unit spawns one UnitCrateType unit created at the cell, or
    // falls back to money when the type is unknown or the limit is reached.
    // Explosion damages the collector. Returns the applied effect.
    // TODO-ORIGINAL 00476CC0 crate pickup: the original also plays sounds and animations,
    // scatters area damage, and respawns crates; not modeled here.
    struct Applied {
        CrateKind kind = CrateKind::Money;
        std::int32_t credits = 0;                  // Money and Money fallback.
        std::vector<std::uint32_t> affected;       // Biased, healed or promoted.
        std::uint32_t created_unit = 0;            // Unit crate.
    };
    Applied collect(const rules::IniFile& rules, session::world::Snapshot& world,
        const CrateRules& crate_rules, std::uint32_t collector_id,
        std::uint32_t cell_index);
    // Crate area biases from Armor/Speed/Firepower crates, default 1.0.
    double firepower_bias(std::uint32_t entity_id) const;
    double armor_bias(std::uint32_t entity_id) const;
    double speed_bias(std::uint32_t entity_id) const;
    bool cloakable(std::uint32_t entity_id) const;
private:
    struct Biases { double firepower = 1.0, armor = 1.0, speed = 1.0; };
    std::map<std::uint32_t, Crate> crates_;
    std::map<std::uint32_t, Biases> biases_;
    std::map<std::uint32_t, bool> cloakable_;
    std::int32_t money_fallback(const rules::IniFile& rules,
        session::world::Snapshot& world, std::uint32_t collector_id,
        const CrateRules& crate_rules, Applied& applied);
};

} // namespace ra2::sim
