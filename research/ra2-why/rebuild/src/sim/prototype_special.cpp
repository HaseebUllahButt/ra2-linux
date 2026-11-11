#include "prototype_special.h"

#include "../sim/sim.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ra2::sim {
namespace {
using rules::IniFile;
using WorldEntity = session::world::Entity;
using WorldSnapshot = session::world::Snapshot;
using session::world::House;
using session::world::Lifetime;

constexpr double rank_veteran_value = 1.0; // 0074FF90 threshold.
constexpr double rank_elite_value = 2.0;   // 00750010 threshold.
// TODO-ORIGINAL 00476CC0 crate pickup: the explosion crate damage is a runtime table
// entry (CrateData); the default here is a playable stand-in.
constexpr std::int32_t crate_explosion_damage = 400;
// TODO-ORIGINAL 00476CC0 crate pickup: the armor crate multiplier is CrateData/256; the
// default here follows the [General] VeteranArmor shape as a stand-in.
constexpr double crate_armor_multiplier = 1.5;
constexpr double crate_speed_multiplier = 1.2;
constexpr double crate_firepower_multiplier = 1.1;

bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

const WorldEntity* find_entity(const WorldSnapshot& world, std::uint32_t id,
                          const char* what) {
    if (id == 0)
        throw std::invalid_argument(std::string(what) + ": zero is not an entity id");
    const auto it = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const WorldEntity& entity) { return entity.id == id; });
    if (it == world.entities.end())
        throw std::invalid_argument(std::string(what) + ": unknown entity");
    return &*it;
}

WorldEntity* find_entity(WorldSnapshot& world, std::uint32_t id, const char* what) {
    return const_cast<WorldEntity*>(find_entity(const_cast<const WorldSnapshot&>(world), id, what));
}

// Live on the map: not destroyed and positive health.
bool live_entity(const WorldEntity& entity) {
    return entity.lifetime != Lifetime::Destroyed && entity.resolved_hp.value_or(1) > 0;
}

bool live_structure(const WorldEntity& entity) {
    return entity.kind == session::world::Kind::Structure && live_entity(entity);
}

bool live_infantry(const WorldEntity& entity) {
    return entity.kind == session::world::Kind::Infantry && live_entity(entity);
}

const House* find_house(const WorldSnapshot& world, std::uint32_t id) {
    const auto it = std::find_if(world.houses.begin(), world.houses.end(),
        [&](const House& house) { return house.id == id; });
    return it == world.houses.end() ? nullptr : &*it;
}

House* find_house(WorldSnapshot& world, std::uint32_t id) {
    return const_cast<House*>(find_house(const_cast<const WorldSnapshot&>(world), id));
}

std::int32_t type_cost(const IniFile& rules, const std::string& type) {
    return rules.read_integer(type, "Cost", 0);
}

ra2map::Cell unpack_cell(std::uint32_t cell_index) {
    return ra2map::Cell{static_cast<std::uint16_t>(cell_index % 512u),
                        static_cast<std::uint16_t>(cell_index / 512u)};
}

std::uint32_t cell_distance_squared(const ra2map::Cell& a, const ra2map::Cell& b) {
    const auto dx = static_cast<std::int32_t>(a.x) - static_cast<std::int32_t>(b.x);
    const auto dy = static_cast<std::int32_t>(a.y) - static_cast<std::int32_t>(b.y);
    return static_cast<std::uint32_t>(dx * dx + dy * dy);
}

std::uint32_t next_entity_id(const WorldSnapshot& world) {
    std::uint32_t top = 0;
    for (const auto& entity : world.entities) top = std::max(top, entity.id);
    return top + 1;
}

// Allied houses: same house, or a set alliance bit when both houses carry the
// original index and alliance mask (004F9A50 shape).
bool allied_houses(const WorldSnapshot& world, std::uint32_t a, std::uint32_t b) {
    if (a == b) return true;
    const auto* house_a = find_house(world, a);
    const auto* house_b = find_house(world, b);
    if (!house_a || !house_b) return false;
    const auto& mask_a = house_a->alliance_mask_5674;
    const auto& mask_b = house_b->alliance_mask_5674;
    const auto& index_a = house_a->original_index;
    const auto& index_b = house_b->original_index;
    if (mask_a && index_b && *mask_a & (1u << *index_b)) return true;
    if (mask_b && index_a && *mask_b & (1u << *index_a)) return true;
    return false;
}

// Ability name table in AbilitiesStruct order.
struct AbilityName { const char* name; Ability ability; };
constexpr AbilityName ability_names[] = {
    {"FASTER", AbilityFaster},         {"STRONGER", AbilityStronger},
    {"FIREPOWER", AbilityFirepower},   {"SCATTER", AbilityScatter},
    {"ROF", AbilityRof},               {"SIGHT", AbilitySight},
    {"CLOAK", AbilityCloak},           {"TIBERIUM_PROOF", AbilityTiberiumProof},
    {"VEIN_PROOF", AbilityVeinProof},  {"SELF_HEAL", AbilitySelfHeal},
    {"EXPLODES", AbilityExplodes},     {"RADAR_INVISIBLE", AbilityRadarInvisible},
    {"SENSORS", AbilitySensors},       {"FEARLESS", AbilityFearless},
    {"C4", AbilityC4},                 {"TIBERIUM_HEAL", AbilityTiberiumHeal},
    {"GUARD_AREA", AbilityGuardArea},  {"CRUSHER", AbilityCrusher},
};

Abilities read_ability_list(const IniFile& rules, std::string_view type,
                            const char* key) {
    Abilities result;
    for (const auto& name : rules.read_list(type, key)) {
        const auto match = std::find_if(std::begin(ability_names), std::end(ability_names),
            [&](const AbilityName& entry) { return equal_name(entry.name, name); });
        if (match == std::end(ability_names))
            throw std::runtime_error("Unknown veteran ability: " + name);
        result.flag[match->ability] = true;
    }
    return result;
}

// One promotion step of the original veterancy crate (00476CC0 crate pickup case 14):
// veteran becomes elite, rookie becomes veteran, negative resets.
void promote_one_step(WorldEntity& entity) {
    const double value = entity.veterancy.value_or(0.0);
    if (value >= rank_veteran_value && value < rank_elite_value)
        entity.veterancy = rank_elite_value;                    // 007500B0 SetElite
    else if (value >= 0.0 && value < rank_veteran_value)
        entity.veterancy = rank_veteran_value;                  // 00750090 SetVeteran
    else if (value < 0.0)
        entity.veterancy = 0.0;                                 // 00750080 Reset
}

// Crate area test: cell distance below the radius, like the original lepton
// distance against CrateRadius (00476CC0 crate pickup armor/speed/firepower cases).
// Base 004783E4 firepower case and the armor case beside it: the
// integer 3D distance from the crate cell center (cell*256+128, the
// cell floor height) to the object coords must be strictly less than
// the CrateRadius leptons at Rules 0+1394.
bool within_crate_radius(const ra2map::Cell& a, const ra2map::Cell& b, double radius) {
    return std::sqrt(static_cast<double>(cell_distance_squared(a, b))) < radius;
}

// Integer 3D crate distance in leptons, crate cell center and floor.
int crate_distance_leptons(const WorldSnapshot& world, const ra2map::Cell& crate,
    const sim::Coord& pos) {
    const auto index = static_cast<std::uint32_t>(crate.y * 512 + crate.x);
    int floor = 0;
    const auto it = world.cells.find(index);
    if (it != world.cells.end() && it->second.terrain && it->second.terrain->floor_world_height) {
        floor = *it->second.terrain->floor_world_height;
    }
    const long long dx = 256LL * crate.x + 128 - pos.x;
    const long long dy = 256LL * crate.y + 128 - pos.y;
    const long long dz = floor - pos.z;
    // FIDELITY GAP: the original distance routine is not traced; this
    // std::sqrt stand-in may differ from it by one lepton at boundaries.
    return static_cast<int>(std::sqrt(double(dx * dx + dy * dy + dz * dz)));
}

void consume_entity(WorldSnapshot& world, std::uint32_t id) {
    auto* entity = find_entity(world, id, "consumed");
    entity->lifetime = Lifetime::Destroyed;
    entity->resolved_hp = 0;
}
} // namespace

// ---------------------------------------------------------------------------
// Veterancy
// ---------------------------------------------------------------------------

VeterancyRules VeterancyRules::load(const IniFile& rules) {
    VeterancyRules result;
    result.veteran_ratio = rules.read_double("General", "VeteranRatio", result.veteran_ratio);
    result.veteran_combat = rules.read_double("General", "VeteranCombat", result.veteran_combat);
    result.veteran_speed = rules.read_double("General", "VeteranSpeed", result.veteran_speed);
    result.veteran_sight = rules.read_double("General", "VeteranSight", result.veteran_sight);
    result.veteran_armor = rules.read_double("General", "VeteranArmor", result.veteran_armor);
    result.veteran_rof = rules.read_double("General", "VeteranROF", result.veteran_rof);
    result.veteran_cap = rules.read_double("General", "VeteranCap", result.veteran_cap);
    result.initial_veteran = rules.read_bool("General", "InitialVeteran", result.initial_veteran);
    if (result.veteran_ratio <= 0.0 || result.veteran_cap < 0.0)
        throw std::runtime_error("Invalid [General] veteran rules");
    return result;
}

TypeAbilities load_type_abilities(const IniFile& rules, std::string_view type) {
    TypeAbilities result;
    result.veteran = read_ability_list(rules, type, "VeteranAbilities");
    result.elite = read_ability_list(rules, type, "EliteAbilities");
    return result;
}

Rank rank_of(double veterancy) noexcept {
    // Base FUN007133A0: exactly 1 <= value < 2 is Veteran. Base 00713420:
    // exactly 2 <= value is Elite. No epsilon: the original compares the
    // stored double directly.
    if (veterancy >= rank_elite_value) return Rank::Elite;
    if (veterancy >= rank_veteran_value) return Rank::Veteran;
    return Rank::Rookie;
}

bool trainable(const IniFile& rules, std::string_view type, session::world::Kind kind) {
    // TechnoTypeClass ctor 00710AF0 defaults Trainable to true; the
    // BuildingTypeClass ctor 0045DD90 defaults it to false. Trainable=
    // overrides both.
    if (rules.contains(type, "Trainable")) return rules.read_bool(type, "Trainable", false);
    return kind != session::world::Kind::Structure;
}

Promotion record_kill(const IniFile& rules, const VeterancyRules& general,
    WorldSnapshot& world, std::uint32_t killer_id, std::uint32_t victim_id) {
    const auto* killer = find_entity(world, killer_id, "killer");
    const auto* victim = find_entity(world, victim_id, "victim");
    if (!live_entity(*killer))
        throw std::invalid_argument("killer is not live");
    if (!live_entity(*victim))
        throw std::invalid_argument("victim is not live");
    if (killer->house_id == victim->house_id)
        throw std::invalid_argument("killer and victim share a house");
    if (!trainable(rules, killer->type, killer->kind)) return {};
    const auto killer_cost = type_cost(rules, killer->type);
    const auto victim_cost = type_cost(rules, victim->type);
    if (killer_cost <= 0 || victim_cost <= 0)
        throw std::invalid_argument("killer or victim type has no Cost");
    auto* mutable_killer = find_entity(world, killer_id, "killer");
    const Rank before = rank_of(mutable_killer->veterancy.value_or(0.0));
    // 0074FF50: value += victimCost / (ownerCost * VeteranRatio), clamp cap.
    double value = mutable_killer->veterancy.value_or(0.0) +
        static_cast<double>(victim_cost) /
        (static_cast<double>(killer_cost) * general.veteran_ratio);
    if (value > general.veteran_cap) value = general.veteran_cap;
    mutable_killer->veterancy = value;
    const Rank after = rank_of(value);
    return {before != after, before, after, value};
}

double veteran_firepower_multiplier(const TypeAbilities& abilities, Rank rank,
    const VeterancyRules& general) noexcept {
    if (rank == Rank::Rookie) return 1.0;
    if (rank == Rank::Veteran)
        return abilities.veteran.flag[AbilityFirepower] ? general.veteran_combat : 1.0;
    return abilities.veteran.flag[AbilityFirepower] || abilities.elite.flag[AbilityFirepower]
        ? general.veteran_combat : 1.0;
}

double veteran_armor_divisor(const TypeAbilities& abilities, Rank rank,
    const VeterancyRules& general) noexcept {
    if (rank == Rank::Rookie) return 1.0;
    if (rank == Rank::Veteran)
        return abilities.veteran.flag[AbilityStronger] ? general.veteran_armor : 1.0;
    return abilities.veteran.flag[AbilityStronger] || abilities.elite.flag[AbilityStronger]
        ? general.veteran_armor : 1.0;
}

double veteran_rof_multiplier(const TypeAbilities& abilities, Rank rank,
    const VeterancyRules& general) noexcept {
    if (rank == Rank::Rookie) return 1.0;
    if (rank == Rank::Veteran)
        return abilities.veteran.flag[AbilityRof] ? general.veteran_rof : 1.0;
    return abilities.veteran.flag[AbilityRof] || abilities.elite.flag[AbilityRof]
        ? general.veteran_rof : 1.0;
}

double veteran_speed_multiplier(const TypeAbilities& abilities, Rank rank,
    const VeterancyRules& general) noexcept {
    if (rank == Rank::Rookie) return 1.0;
    if (rank == Rank::Veteran)
        return abilities.veteran.flag[AbilityFaster] ? general.veteran_speed : 1.0;
    return abilities.veteran.flag[AbilityFaster] || abilities.elite.flag[AbilityFaster]
        ? general.veteran_speed : 1.0;
}

double veteran_sight_multiplier(const TypeAbilities& abilities, Rank rank,
    const VeterancyRules& general) noexcept {
    if (rank == Rank::Rookie || general.veteran_sight <= 0.0) return 1.0;
    if (rank == Rank::Veteran)
        return abilities.veteran.flag[AbilitySight] ? general.veteran_sight : 1.0;
    return abilities.veteran.flag[AbilitySight] || abilities.elite.flag[AbilitySight]
        ? general.veteran_sight : 1.0;
}

// ---------------------------------------------------------------------------
// Garrison
// ---------------------------------------------------------------------------

GarrisonRules GarrisonRules::load(const IniFile& rules, std::string_view type) {
    GarrisonRules result;
    result.can_be_occupied = rules.read_bool(type, "CanBeOccupied", false);
    result.can_occupy_fire = rules.read_bool(type, "CanOccupyFire", result.can_be_occupied);
    // TODO-ORIGINAL BuildingTypeClass ini reader: base rules.ini has no CanOccupyFire; assume occupied buildings fire.
    result.max_occupants = rules.read_integer(type, "MaxNumberOccupants", 0);
    return result;
}

bool GarrisonState::garrison(const IniFile& rules, WorldSnapshot& world,
    std::uint32_t building_id, std::uint32_t infantry_id) {
    const auto* building = find_entity(world, building_id, "building");
    const auto* infantry = find_entity(world, infantry_id, "infantry");
    if (!live_structure(*building) || !live_infantry(*infantry)) return false;
    const auto garrison_rules = GarrisonRules::load(rules, building->type);
    if (!garrison_rules.can_be_occupied) return false;
    // 00457CE0: own or allied house only, and room for one more occupant.
    if (!allied_houses(world, infantry->house_id, building->house_id)) return false;
    if (static_cast<std::int32_t>(occupant_count(building_id)) >=
        garrison_rules.max_occupants) return false;
    auto& list = occupants_[building_id];
    if (std::find(list.begin(), list.end(), infantry_id) != list.end()) return false;
    // The infantry type must be an Occupier (00522910 type +0xEB4).
    if (!rules.read_bool(infantry->type, "Occupier", false)) return false;
    auto* mutable_infantry = find_entity(world, infantry_id, "infantry");
    mutable_infantry->in_limbo = true; // Leaves the ground layer.
    if (building->cell) mutable_infantry->cell = building->cell;
    list.push_back(infantry_id);
    return true;
}

std::vector<std::uint32_t> GarrisonState::ungarrison(WorldSnapshot& world,
    std::uint32_t building_id) {
    std::vector<std::uint32_t> leaving;
    const auto it = occupants_.find(building_id);
    if (it == occupants_.end()) return leaving;
    leaving = std::move(it->second);
    occupants_.erase(it);
    for (const auto id : leaving) {
        const auto* infantry = find_entity(world, id, "infantry");
        if (infantry->lifetime == Lifetime::Destroyed) continue;
        auto* mutable_infantry = find_entity(world, id, "infantry");
        mutable_infantry->in_limbo = false;
    }
    return leaving;
}

std::vector<std::uint32_t> GarrisonState::occupants(std::uint32_t building_id) const {
    const auto it = occupants_.find(building_id);
    return it == occupants_.end() ? std::vector<std::uint32_t>{} : it->second;
}

std::size_t GarrisonState::occupant_count(std::uint32_t building_id) const {
    const auto it = occupants_.find(building_id);
    return it == occupants_.end() ? 0 : it->second.size();
}

std::int32_t GarrisonState::occupant_fire_damage(const WorldSnapshot& world,
    const IniFile& rules, std::uint32_t building_id) const {
    const auto* building = find_entity(world, building_id, "building");
    const auto garrison_rules = GarrisonRules::load(rules, building->type);
    if (!garrison_rules.can_occupy_fire) return 0;
    std::int32_t total = 0;
    for (const auto id : occupants(building_id)) {
        const auto* infantry = find_entity(world, id, "infantry");
        if (!live_infantry(*infantry)) continue;
        const auto weapon = rules.read_string(infantry->type, "Primary");
        if (weapon.empty()) continue;
        total += rules.read_integer(weapon, "Damage", 0);
    }
    return total;
}

bool GarrisonState::purge_destroyed(const WorldSnapshot& world) {
    bool dropped = false;
    for (auto it = occupants_.begin(); it != occupants_.end();) {
        auto& list = it->second;
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](std::uint32_t id) {
            const auto entity = std::find_if(world.entities.begin(), world.entities.end(),
                [&](const WorldEntity& item) { return item.id == id; });
            return entity == world.entities.end() || !live_infantry(*entity);
        }), list.end());
        dropped = dropped || list.size() != before;
        if (list.empty()) it = occupants_.erase(it);
        else ++it;
    }
    return dropped;
}

// ---------------------------------------------------------------------------
// Engineer
// ---------------------------------------------------------------------------

EngineerResult engineer_enter_building(const IniFile& rules, WorldSnapshot& world,
    std::uint32_t engineer_id, std::uint32_t building_id,
    double engineer_capture_level) {
    const auto* engineer = find_entity(world, engineer_id, "engineer");
    const auto* building = find_entity(world, building_id, "building");
    if (!live_infantry(*engineer))
        throw std::invalid_argument("engineer is not a live infantry");
    if (!live_structure(*building))
        throw std::invalid_argument("building is not a live structure");
    if (!rules.read_bool(engineer->type, "Engineer", false)) return {};
    if (allied_houses(world, engineer->house_id, building->house_id)) {
        // Friendly building: full repair (RA1 Renovate), engineer consumed.
        auto* mutable_building = find_entity(world, building_id, "building");
        mutable_building->resolved_hp = mutable_building->maximum_hp.value_or(
            mutable_building->resolved_hp.value_or(0));
        consume_entity(world, engineer_id);
        return {EngineerEffect::RepairedAlly, 0};
    }
    const auto max_hp = building->maximum_hp.value_or(building->resolved_hp.value_or(0));
    const auto hp = building->resolved_hp.value_or(0);
    EngineerResult result;
    if (max_hp > 0 && static_cast<double>(hp) / static_cast<double>(max_hp) <=
        engineer_capture_level) {
        // Capture: the building changes owner, the engineer is consumed.
        auto* mutable_building = find_entity(world, building_id, "building");
        mutable_building->house_id = engineer->house_id;
        result.effect = EngineerEffect::Captured;
    } else {
        // Damage: never destroys, the engineer is consumed.
        auto* mutable_building = find_entity(world, building_id, "building");
        const auto damage = std::min<std::int32_t>(max_hp / 3, std::max(hp - 1, 0));
        mutable_building->resolved_hp = std::max(hp - damage, 0);
        result.damage = damage;
        result.effect = EngineerEffect::Damaged;
    }
    consume_entity(world, engineer_id);
    return result;
}

bool engineer_repair_bridge(WorldSnapshot& world,
    std::map<std::uint32_t, bool>& broken_bridges, std::uint32_t engineer_id,
    std::uint32_t cell_index) {
    const auto* engineer = find_entity(world, engineer_id, "engineer");
    if (!live_infantry(*engineer) || !engineer->cell) return false;
    const auto engineer_cell = *engineer->cell;
    const auto dx = static_cast<std::int32_t>(engineer_cell.x) -
        static_cast<std::int32_t>(cell_index % 512u);
    const auto dy = static_cast<std::int32_t>(engineer_cell.y) -
        static_cast<std::int32_t>(cell_index / 512u);
    // Same cell or one step away.
    if (std::abs(dx) > 1 || std::abs(dy) > 1) return false;
    const auto it = broken_bridges.find(cell_index);
    if (it == broken_bridges.end() || !it->second) return false;
    it->second = false;
    const auto cell_it = world.cells.find(cell_index);
    if (cell_it != world.cells.end()) cell_it->second.usable = true;
    consume_entity(world, engineer_id);
    return true;
}

// ---------------------------------------------------------------------------
// Spy
// ---------------------------------------------------------------------------

void SpyInfiltrations::tick(std::int32_t frames) {
    auto reduce = [frames](std::map<std::uint32_t, std::int32_t>& timers) {
        for (auto it = timers.begin(); it != timers.end();) {
            it->second -= frames;
            if (it->second <= 0) it = timers.erase(it);
            else ++it;
        }
    };
    reduce(power_outage_frames);
    reduce(radar_off_frames);
}

SpyEffect spy_infiltrate(const IniFile& rules, WorldSnapshot& world, SpyInfiltrations& state,
    std::uint32_t spy_id, std::uint32_t building_id,
    double spy_power_blackout_frames, double spy_money_steal_percent,
    const std::vector<std::string>& lab_types) {
    const auto* spy = find_entity(world, spy_id, "spy");
    const auto* building = find_entity(world, building_id, "building");
    if (!live_infantry(*spy))
        throw std::invalid_argument("spy is not a live infantry");
    if (!live_structure(*building))
        throw std::invalid_argument("building is not a live structure");
    if (!rules.read_bool(spy->type, "Infiltrate", false))
        throw std::invalid_argument("type cannot infiltrate");
    if (spy->house_id == building->house_id)
        throw std::invalid_argument("cannot infiltrate an own building");
    const auto& type = building->type;
    SpyEffect effect;
    if (rules.read_integer(type, "Power", 0) > 0) {
        // Power plant: outage on the owning house (0050BC90).
        state.power_outage_frames[building->house_id] =
            static_cast<std::int32_t>(spy_power_blackout_frames);
        effect.kind = SpyEffectKind::PowerOutage;
    } else if (rules.read_bool(type, "Radar", false)) {
        // Radar dome: radar reset on the owning house (0050BD10). The
        // original also reshrouds their explored map; not modeled here.
        state.radar_off_frames[building->house_id] =
            static_cast<std::int32_t>(spy_power_blackout_frames);
        effect.kind = SpyEffectKind::RadarReset;
    } else if (!rules.read_string(type, "SuperWeapon").empty()) {
        // Superweapon building: charge reset (006CE0B0).
        effect.kind = SpyEffectKind::SuperweaponReset;
    } else {
        std::size_t lab_index = 0;
        const auto lab = std::find_if(lab_types.begin(), lab_types.end(),
            [&](const std::string& name) { ++lab_index; return equal_name(name, type); });
        if (lab != lab_types.end()) {
            // Battle lab: tech unlock for the spy's house (004571E0 list).
            state.unlocked_labs[spy->house_id].push_back(*lab);
            effect.kind = SpyEffectKind::TechUnlock;
            effect.amount = static_cast<std::int32_t>(lab_index - 1);
        } else if (rules.read_bool(type, "ConstructionYard", false)) {
            // Construction yard: cash steal from the owning house
            // (004F9790/004F9950 pair).
            const auto* victim_house = find_house(world, building->house_id);
            const auto cash = victim_house ? victim_house->cash.value_or(0) : 0;
            const auto stolen = static_cast<std::int32_t>(
                static_cast<double>(cash) * spy_money_steal_percent);
            if (stolen > 0) {
                find_house(world, building->house_id)->cash = cash - stolen;
                if (auto* spy_house = find_house(world, spy->house_id))
                    spy_house->cash = spy_house->cash.value_or(0) + stolen;
            }
            effect.kind = SpyEffectKind::MoneyStolen;
            effect.amount = stolen;
        }
    }
    consume_entity(world, spy_id);
    return effect;
}

// ---------------------------------------------------------------------------
// Crates
// ---------------------------------------------------------------------------


// Base 0064D490: Powerups entries split by commas; the fourth field is
// the power-up factor, stored as a double in DAT_00851080. A trailing
// percent sign divides by 100 (FUN_00784130 checks for 0x25).
double powerup_factor(const rules::IniFile& rules, std::string_view name,
    double fallback) {
    const auto value = rules.read_string("Powerups", name, "");
    if (value.empty()) return fallback;
    const auto fields = rules::IniFile::split_list(value);
    if (fields.size() < 4) return fallback;
    auto text = fields[3];
    bool percent = false;
    if (!text.empty() && text.back() == '%') {
        percent = true;
        text.pop_back();
    }
    try {
        const double parsed = std::stod(std::string(text));
        return percent ? parsed / 100.0 : parsed;
    } catch (const std::exception&) {
        return fallback;
    }
}

CrateRules CrateRules::load(const IniFile& rules) {
    CrateRules result;
    // RA1 CONST.CPP share defaults; the RA2 table is a runtime table.
    static const std::int32_t default_shares[] = {50, 20, 1, 10, 10, 10, 10, 3, 5};
    static_assert(std::size(default_shares) ==
        static_cast<std::size_t>(CrateKind::CrateKindCount));
    for (std::size_t i = 0; i < std::size(default_shares); ++i)
        result.share[i] = default_shares[i];
    result.radius = rules.read_double("CrateRules", "CrateRadius", result.radius);
    result.crate_maximum = rules.read_integer("CrateRules", "CrateMaximum", result.crate_maximum);
    result.crate_minimum = rules.read_integer("CrateRules", "CrateMinimum", result.crate_minimum);
    result.solo_money = rules.read_integer("CrateRules", "SoloCrateMoney", result.solo_money);
    result.regen_minutes = rules.read_double("CrateRules", "CrateRegen", result.regen_minutes);
    result.unit_crate_type = rules.read_string("CrateRules", "UnitCrateType");
    result.free_mcv = rules.read_bool("CrateRules", "FreeMCV", result.free_mcv);
    result.armor_crate = powerup_factor(rules, "Armor", result.armor_crate);
    result.speed_crate = powerup_factor(rules, "Speed", result.speed_crate);
    result.firepower_crate = powerup_factor(rules, "Firepower", result.firepower_crate);
    if (equal_name(result.unit_crate_type, "none")) result.unit_crate_type.clear();
    if (result.radius <= 0.0 || result.crate_maximum < 0 || result.crate_minimum < 0 ||
        result.regen_minutes < 0.0)
        throw std::runtime_error("Invalid [CrateRules]");
    return result;
}

CrateKind CrateState::random_kind(WorldSnapshot& world, const CrateRules& rules) const {
    sim::Random rng(world.random);
    std::int32_t total = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(CrateKind::CrateKindCount); ++i)
        total += rules.share[i];
    auto picked = CrateKind::Money;
    if (total > 0) {
        const auto draw = rng.ranged(1, total);
        std::int32_t seen = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(CrateKind::CrateKindCount); ++i) {
            seen += rules.share[i];
            if (draw <= seen) {
                picked = static_cast<CrateKind>(i);
                break;
            }
        }
    }
    world.random = rng.state();
    return picked;
}

bool CrateState::place(std::uint32_t cell_index, CrateKind kind,
    std::uint32_t now_frame, const CrateRules& rules) {
    if (crates_.count(cell_index)) return false;
    if (static_cast<std::int32_t>(crates_.size()) >= rules.crate_maximum) return false;
    // Original regen timer: minutes at 900 frames per minute (foot_path.cpp).
    const auto expire = now_frame +
        static_cast<std::uint32_t>(rules.regen_minutes * 900.0);
    crates_.emplace(cell_index, Crate{kind, expire});
    return true;
}

std::vector<std::uint32_t> CrateState::expire(std::uint32_t now_frame) {
    std::vector<std::uint32_t> expired;
    for (auto it = crates_.begin(); it != crates_.end();) {
        if (now_frame >= it->second.expire_frame) {
            expired.push_back(it->first);
            it = crates_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

const Crate* CrateState::find(std::uint32_t cell_index) const {
    const auto it = crates_.find(cell_index);
    return it == crates_.end() ? nullptr : &it->second;
}

bool CrateState::unit_limit_reached(const WorldSnapshot& world, std::uint32_t house_id) {
    std::size_t units = 0;
    for (const auto& entity : world.entities)
        if (entity.house_id == house_id && entity.kind == session::world::Kind::Unit &&
            entity.lifetime != Lifetime::Destroyed)
            ++units;
    return units > 50; // 00476CC0 crate pickup case 1.
}

std::int32_t CrateState::money_fallback(const IniFile& rules, WorldSnapshot& world,
    std::uint32_t collector_id, const CrateRules& crate_rules, Applied& applied) {
    const auto* collector = find_entity(world, collector_id, "collector");
    auto* house = find_house(world, collector->house_id);
    if (!house) throw std::invalid_argument("collector house is unknown");
    // 00476CC0 crate pickup money case: random amount; SoloCrateMoney in solo play.
    sim::Random rng(world.random);
    const auto credits = rng.ranged(0, 900);
    world.random = rng.state();
    (void)rules;
    (void)crate_rules;
    house->cash = house->cash.value_or(0) + credits;
    applied.kind = CrateKind::Money;
    applied.credits = credits;
    return credits;
}

CrateState::Applied CrateState::collect(const IniFile& rules, WorldSnapshot& world,
    const CrateRules& crate_rules, std::uint32_t collector_id,
    std::uint32_t cell_index) {
    const auto* collector = find_entity(world, collector_id, "collector");
    if (!live_entity(*collector))
        throw std::invalid_argument("collector is not live");
    const auto crate_it = crates_.find(cell_index);
    if (crate_it == crates_.end())
        throw std::invalid_argument("no crate at the cell");
    const auto kind = crate_it->second.kind;
    crates_.erase(crate_it);
    const auto crate_cell = unpack_cell(cell_index);
    Applied applied;
    applied.kind = kind;
    switch (kind) {
        case CrateKind::Money:
            money_fallback(rules, world, collector_id, crate_rules, applied);
            break;
        case CrateKind::Unit: {
            // 00476CC0 crate pickup unit case: too many units falls back to money.
            if (unit_limit_reached(world, collector->house_id) ||
                crate_rules.unit_crate_type.empty()) {
                money_fallback(rules, world, collector_id, crate_rules, applied);
                break;
            }
            WorldEntity created;
            created.id = next_entity_id(world);
            created.house_id = collector->house_id;
            created.kind = session::world::Kind::Unit;
            created.lifetime = Lifetime::Created;
            created.type = crate_rules.unit_crate_type;
            created.cell = collector->cell;
            created.resolved_hp = rules.read_integer(created.type, "Strength", 0);
            world.creation_order.push_back(
                session::world::CreatedObject{session::world::CreatedKind::Entity, created.id});
            world.entities.push_back(std::move(created));
            applied.created_unit = world.entities.back().id;
            break;
        }
        case CrateKind::HealBase:
            // 00476CC0 crate pickup heal base case: all objects of the collector's house.
            for (auto& entity : world.entities) {
                if (entity.house_id != collector->house_id ||
                    !live_entity(entity) || !entity.maximum_hp) continue;
                entity.resolved_hp = entity.maximum_hp;
                applied.affected.push_back(entity.id);
            }
            break;
        case CrateKind::Armor:
        case CrateKind::Speed:
        case CrateKind::Firepower: {
            // 00476CC0 crate pickup area cases: every live techno in radius with a 1.0
            // bias gains the crate multiplier.
            const double multiplier = kind == CrateKind::Armor ? crate_rules.armor_crate
                : kind == CrateKind::Speed ? crate_rules.speed_crate
                : crate_rules.firepower_crate;
            for (auto& entity : world.entities) {
                if (!live_entity(entity) || !entity.cell) continue;
                // Cell presence does not guarantee coords; an object
                // without a position is not in the original scan list.
                if (!entity.position) continue;
                // A loaded upgraded actor keeps its own multiplier; the
                // crate never rewrites an existing bonus.
                if ((kind == CrateKind::Armor && entity.armor_multiplier &&
                     *entity.armor_multiplier != 1.0) ||
                    (kind == CrateKind::Firepower && entity.firepower_multiplier &&
                     *entity.firepower_multiplier != 1.0))
                    continue;
                if (entity.kind != session::world::Kind::Unit && entity.kind != session::world::Kind::Infantry &&
                    entity.kind != session::world::Kind::Structure) continue;
                if (crate_distance_leptons(world, crate_cell, *entity.position) >=
                    static_cast<int>(crate_rules.radius * 256.0))
                    continue;
                auto& biases = biases_[entity.id];
                double& bias = kind == CrateKind::Armor ? biases.armor
                    : kind == CrateKind::Speed ? biases.speed : biases.firepower;
                if (bias != 1.0) continue;
                bias = multiplier;
                // The damage path reads the resolved entity fields, so the
                // armor and firepower crates write them directly.
                // The original requires the field to be exactly 1.0
                // and multiplies the factor into it (004783E4).
                if (kind == CrateKind::Armor) {
                    if (entity.armor_multiplier.value_or(1.0) != 1.0) continue;
                    entity.armor_multiplier = multiplier *
                        entity.armor_multiplier.value_or(1.0);
                } else if (kind == CrateKind::Firepower) {
                    if (entity.firepower_multiplier.value_or(1.0) != 1.0) continue;
                    entity.firepower_multiplier = multiplier *
                        entity.firepower_multiplier.value_or(1.0);
                }
                applied.affected.push_back(entity.id);
            }
            break;
        }
        case CrateKind::Veterancy: {
            // 00476CC0 crate pickup veterancy case: one rank for trainable technos in radius.
            for (auto& entity : world.entities) {
                if (!live_entity(entity) || !entity.cell) continue;
                if (entity.kind == session::world::Kind::Structure) continue;
                if (!within_crate_radius(*entity.cell, crate_cell, crate_rules.radius))
                    continue;
                if (!trainable(rules, entity.type, entity.kind)) continue;
                const Rank before = rank_of(entity.veterancy.value_or(0.0));
                promote_one_step(entity);
                if (rank_of(entity.veterancy.value_or(0.0)) != before)
                    applied.affected.push_back(entity.id);
            }
            break;
        }
        case CrateKind::Cloak: {
            // 00476CC0 crate pickup cloak case: technos in radius become cloakable.
            for (auto& entity : world.entities) {
                if (!live_entity(entity) || !entity.cell) continue;
                if (entity.kind != session::world::Kind::Unit && entity.kind != session::world::Kind::Infantry &&
                    entity.kind != session::world::Kind::Structure) continue;
                if (!within_crate_radius(*entity.cell, crate_cell, crate_rules.radius))
                    continue;
                cloakable_[entity.id] = true;
                applied.affected.push_back(entity.id);
            }
            break;
        }
        case CrateKind::Explosion: {
            // 00476CC0 crate pickup explosion case: the collector takes the crate damage.
            // TODO-ORIGINAL: the original also scatters five area explosions.
            auto* mutable_collector = find_entity(world, collector_id, "collector");
            mutable_collector->resolved_hp = std::max(
                collector->resolved_hp.value_or(0) - crate_explosion_damage, 0);
            applied.affected.push_back(collector_id);
            break;
        }
        case CrateKind::CrateKindCount:
            break;
    }
    return applied;
}

double CrateState::firepower_bias(std::uint32_t entity_id) const {
    const auto it = biases_.find(entity_id);
    return it == biases_.end() ? 1.0 : it->second.firepower;
}

double CrateState::armor_bias(std::uint32_t entity_id) const {
    const auto it = biases_.find(entity_id);
    return it == biases_.end() ? 1.0 : it->second.armor;
}

double CrateState::speed_bias(std::uint32_t entity_id) const {
    const auto it = biases_.find(entity_id);
    return it == biases_.end() ? 1.0 : it->second.speed;
}

bool CrateState::cloakable(std::uint32_t entity_id) const {
    const auto it = cloakable_.find(entity_id);
    return it != cloakable_.end() && it->second;
}

} // namespace ra2::sim
