#pragma once
// Shared by prototype_ai.cpp (house AI) and prototype_ai_team.cpp (teams).
#include "prototype_ai.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace ra2::sim {
namespace ai_detail {
using WorldEntity = session::world::Entity;
using WorldHouse = session::world::House;
using WorldKind = session::world::Kind;
using session::world::Lifetime;
using rules::IniFile;

inline bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

inline bool contains(const std::vector<std::string>& names, std::string_view name) {
    return std::any_of(names.begin(), names.end(), [&](const auto& item) { return equal_name(item, name); });
}

inline bool live(const WorldEntity& entity) {
    return entity.id != 0 && entity.lifetime == Lifetime::Placed && entity.cell &&
        entity.resolved_hp.value_or(0) > 0 && entity.active.value_or(false) &&
        !entity.in_limbo.value_or(true) && entity.on_map != false;
}

inline bool registered(const IniFile& rules, const char* registry, const std::string& type) {
    const auto* section = rules.find_section(registry);
    return section && std::any_of(section->entries.begin(), section->entries.end(),
        [&](const auto& item) { return equal_name(item.value, type); });
}

inline bool yard(const IniFile& rules, const WorldEntity& entity) {
    return entity.kind == WorldKind::Structure && rules.read_bool(entity.type, "ConstructionYard");
}

inline bool mcv(const IniFile& rules, const WorldEntity& entity) {
    const auto building = rules.read_string(entity.type, "DeploysInto");
    return entity.kind == WorldKind::Unit && registered(rules, "BuildingTypes", building) &&
        rules.read_bool(building, "ConstructionYard");
}

inline bool ground_weapon(const IniFile& rules, const std::string& type) {
    const auto weapon = rules.read_string(type, "Primary");
    const auto projectile = rules.read_string(weapon, "Projectile");
    // TODO-ORIGINAL: Only direct, positive primary ground damage is considered.
    // Secondary selection, special weapons and air/naval tactics need original AI.
    return rules.read_integer(weapon, "Damage") > 0 && !rules.read_bool(weapon, "LimboLaunch") &&
        !projectile.empty() && rules.find_section(projectile) && rules.read_bool(projectile, "AG", true);
}

inline bool fighter(const IniFile& rules, const WorldEntity& entity) {
    return entity.kind == WorldKind::Unit && rules.read_integer(entity.type, "Speed") > 0 &&
        !rules.read_bool(entity.type, "Naval") && !rules.read_bool(entity.type, "Harvester") &&
        !rules.read_bool(entity.type, "Weeder") && !mcv(rules, entity) && ground_weapon(rules, entity.type);
}

inline std::optional<std::int32_t> side_index(const IniFile& rules, const WorldHouse& house) {
    if (house.side_index) return house.side_index;
    const auto* sides = rules.find_section("Sides");
    if (!sides) return {};
    const auto side = rules.read_string(house.country, "Side");
    for (std::size_t i = 0; i < sides->entries.size(); ++i) {
        const auto& entry = sides->entries[i];
        if (equal_name(entry.key, side) || contains(IniFile::split_list(entry.value), house.country))
            return static_cast<std::int32_t>(i);
    }
    return {};
}

using Actors = std::vector<const WorldEntity*>;

inline bool owns(const Actors& buildings, std::string_view type) {
    return std::any_of(buildings.begin(), buildings.end(),
        [&](const WorldEntity* entity) { return equal_name(entity->type, type); });
}

inline bool prerequisite(const IniFile& rules, const Actors& buildings, const std::string& name) {
    constexpr std::array<const char*, 6> groups{{"Power", "Factory", "Barracks", "Radar", "Tech", "Proc"}};
    for (const auto* group : groups) {
        if (!equal_name(name, group)) continue;
        for (const auto& type : rules.read_list("General", std::string("Prerequisite") + group))
            if (owns(buildings, type)) return true;
        return false;
    }
    return owns(buildings, name);
}

// No cash means no money test: the original pays a building in stages
// while it is made (004B9300), and 004E9660 picks the next base node
// without a money test. 004E9C90 tests Available_Money >= cost for units.
inline bool buildable(const IniFile& rules, const WorldHouse& house, const Actors& actors,
               const Actors& buildings, const std::string& type, std::optional<std::int64_t> cash,
               bool plan_node = false) {
    const auto cost = rules.read_integer(type, "Cost", -1);
    const auto tech = rules.read_integer(type, "TechLevel", -1);
    // TODO-ORIGINAL: This is a conservative policy filter, not House::Can_Build.
    // The production runtime remains responsible for the final legality check.
    // Prototype games use level ten when the house has no resolved tech level.
    // A base plan node had its tech test in 004EF890 (TechLevel <= House
    // +11C, so -1 passes), and 004E9660 makes no other: a map that sets
    // TechLevel=-1 on its shipyard still gets one in the computer's plan.
    const bool tech_ok = plan_node || (tech >= 0 && tech <= house.tech_level.value_or(10));
    if (cost <= 0 || (cash && cost > *cash) || !tech_ok ||
        !contains(rules.read_list(type, "Owner"), house.country)) return false;
    const auto required = rules.read_list(type, "RequiredHouses");
    if ((!required.empty() && !contains(required, house.country)) ||
        contains(rules.read_list(type, "ForbiddenHouses"), house.country)) return false;
    if ((rules.read_bool(type, "RequiresStolenAlliedTech") && !house.stolen_tech_205.value_or(false)) ||
        (rules.read_bool(type, "RequiresStolenSovietTech") && !house.stolen_tech_204.value_or(false))) return false;
    const auto limit = rules.read_integer(type, "BuildLimit");
    // Negative limits need historical counters, so this policy avoids them.
    if (limit < 0) return false;
    const auto count = std::count_if(actors.begin(), actors.end(),
        [&](const WorldEntity* entity) { return equal_name(entity->type, type); });
    if (limit > 0 && count >= limit) return false;
    const auto overrides = rules.read_list(type, "PrerequisiteOverride");
    for (const auto& name : overrides) if (prerequisite(rules, buildings, name)) return true;
    for (const auto& name : rules.read_list(type, "Prerequisite"))
        if (!prerequisite(rules, buildings, name)) return false;
    return true;
}

inline const WorldEntity* factory(const IniFile& rules, const Actors& buildings, const char* kind, bool naval = false) {
    const auto found = std::find_if(buildings.begin(), buildings.end(), [&](const WorldEntity* entity) {
        return equal_name(rules.read_string(entity->type, "Factory"), kind) &&
            rules.read_bool(entity->type, "Naval") == naval;
    });
    return found == buildings.end() ? nullptr : *found;
}

inline skirmish::Order order(skirmish::OrderKind kind, std::uint32_t house, const WorldEntity& entity,
                      std::string type = {}) {
    skirmish::Order result;
    result.kind = kind;
    result.house = house;
    result.entity = entity.id;
    result.type = std::move(type);
    result.cell = *entity.cell;
    return result;
}

inline std::uint64_t distance(ra2map::Cell from, ra2map::Cell to) {
    const auto dx = static_cast<std::int64_t>(from.x) - to.x;
    const auto dy = static_cast<std::int64_t>(from.y) - to.y;
    return static_cast<std::uint64_t>(dx * dx + dy * dy);
}
} // namespace ai_detail


namespace ai_detail {
using Trigger = PrototypeAI::Trigger;

// AI.INI lines keep empty fields, so IniFile::split_list does not fit here.
inline std::vector<std::string> fields(std::string_view value) {
    std::vector<std::string> result;
    for (;;) {
        const auto comma = value.find(',');
        result.emplace_back(IniFile::trim(value.substr(0, comma)));
        if (comma == std::string_view::npos) return result;
        value.remove_prefix(comma + 1);
    }
}

inline std::int32_t number(const std::string& text, std::int32_t fallback = 0) {
    return IniFile::integer_value(text).value_or(fallback);
}

// The 64 hex digits store four {int, comparator} pairs as little-endian bytes.
// The original reads only the first pair (TSpp AITriggerTypeClass::Data).
inline std::int32_t hex_word(const std::string& text, std::size_t word) {
    std::uint32_t result = 0;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        const auto at = (word * 4 + byte) * 2;
        if (at + 2 > text.size()) return 0;
        const auto digit = [](char c) -> std::uint32_t {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
        };
        result |= ((digit(text[at]) << 4) | digit(text[at + 1])) << (byte * 8);
    }
    return static_cast<std::int32_t>(result);
}

// TSpp AITriggerComparatorType: <, <=, ==, >=, >, !=.
inline bool compare(std::int64_t value, std::int32_t operand, std::int32_t comparator) {
    switch (comparator) {
    case 0: return value < operand;
    case 1: return value <= operand;
    case 2: return value == operand;
    case 3: return value >= operand;
    case 4: return value > operand;
    case 5: return value != operand;
    default: return false;
    }
}

inline bool none(std::string_view name) { return name.empty() || equal_name(name, "<none>"); }

// Rules lists such as TeamDelays and TotalAITeamCap are hardest first.
inline std::int32_t by_difficulty(const IniFile& rules, const char* key, PrototypeAI::Difficulty level,
                           std::int32_t fallback) {
    const auto values = rules.read_list("General", key);
    const auto index = static_cast<std::size_t>(2 - static_cast<std::int32_t>(level));
    return index < values.size() ? number(values[index], fallback) : fallback;
}

// ScriptTypes quarry numbers, as used by the map trigger prototype.
inline bool quarry(const IniFile& rules, std::int32_t kind, const WorldEntity& entity) {
    const auto& type = entity.type;
    switch (kind) {
    case 2: return entity.kind == WorldKind::Structure;
    case 3: return entity.kind == WorldKind::Unit && rules.read_bool(type, "Harvester");
    case 4: return entity.kind == WorldKind::Infantry;
    case 5: return entity.kind == WorldKind::Unit && !rules.read_bool(type, "Harvester");
    case 6: return entity.kind == WorldKind::Structure && !none(rules.read_string(type, "Factory"));
    case 7: return entity.kind == WorldKind::Structure && !none(rules.read_string(type, "Primary"));
    case 9: return entity.kind == WorldKind::Structure && rules.read_integer(type, "Power") > 0;
    case 11: return entity.kind == WorldKind::Structure && rules.read_bool(type, "Capturable");
    default: return true;
    }
}

inline const char* factory_kind(const IniFile& rules, const std::string& type) {
    if (registered(rules, "InfantryTypes", type)) return "InfantryType";
    if (registered(rules, "VehicleTypes", type)) return "UnitType";
    if (registered(rules, "AircraftTypes", type)) return "AircraftType";
    return nullptr;
}

inline std::uint32_t chebyshev(ra2map::Cell a, ra2map::Cell b) {
    const auto dx = std::abs(static_cast<std::int32_t>(a.x) - b.x);
    const auto dy = std::abs(static_cast<std::int32_t>(a.y) - b.y);
    return static_cast<std::uint32_t>(std::max(dx, dy));
}

inline constexpr std::int32_t frames_per_second = 15; // 006B7AD0 case 5 multiplier
inline constexpr std::int32_t move_timeout_frames = 1800;
} // namespace ai_detail


using namespace ai_detail;

struct PrototypeAI::Context {
    const session::world::Snapshot& world;
    const WorldHouse& computer;
    const WorldHouse& player;
    std::int32_t frame = 0;
    std::optional<std::int32_t> side;
    Actors actors, buildings, enemies;
    std::int64_t cash = 0;
    bool enemy_available = false;
    std::optional<ra2map::Cell> own_centre, enemy_centre; // Base House +53BC
    std::map<std::uint32_t, const WorldEntity*> own; // live own actors by ID
    std::map<std::uint32_t, const WorldEntity*> targets; // live legal enemy targets
    std::vector<std::uint32_t> busy; // factories that got a build order in this decision

    std::int64_t count(std::uint32_t house, const std::string& type) const {
        return std::count_if(world.entities.begin(), world.entities.end(), [&](const WorldEntity& e) {
            return e.house_id == house && live(e) && equal_name(e.type, type);
        });
    }
};


} // namespace ra2::sim
