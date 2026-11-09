#include "prototype_ai.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace ra2::sim {
namespace {
using WorldEntity = session::world::Entity;
using WorldHouse = session::world::House;
using WorldKind = session::world::Kind;
using session::world::Lifetime;
using rules::IniFile;

bool equal_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

bool contains(const std::vector<std::string>& names, std::string_view name) {
    return std::any_of(names.begin(), names.end(), [&](const auto& item) { return equal_name(item, name); });
}

bool live(const WorldEntity& entity) {
    return entity.id != 0 && entity.lifetime == Lifetime::Placed && entity.cell &&
        entity.resolved_hp.value_or(0) > 0 && entity.active.value_or(false) &&
        !entity.in_limbo.value_or(true) && entity.on_map != false;
}

bool registered(const IniFile& rules, const char* registry, const std::string& type) {
    const auto* section = rules.find_section(registry);
    return section && std::any_of(section->entries.begin(), section->entries.end(),
        [&](const auto& item) { return equal_name(item.value, type); });
}

bool yard(const IniFile& rules, const WorldEntity& entity) {
    return entity.kind == WorldKind::Structure && rules.read_bool(entity.type, "ConstructionYard");
}

bool mcv(const IniFile& rules, const WorldEntity& entity) {
    const auto building = rules.read_string(entity.type, "DeploysInto");
    return entity.kind == WorldKind::Unit && registered(rules, "BuildingTypes", building) &&
        rules.read_bool(building, "ConstructionYard");
}

bool ground_weapon(const IniFile& rules, const std::string& type) {
    const auto weapon = rules.read_string(type, "Primary");
    const auto projectile = rules.read_string(weapon, "Projectile");
    // TODO-ORIGINAL: Only direct, positive primary ground damage is considered.
    // Secondary selection, special weapons and air/naval tactics need original AI.
    return rules.read_integer(weapon, "Damage") > 0 && !rules.read_bool(weapon, "LimboLaunch") &&
        !projectile.empty() && rules.find_section(projectile) && rules.read_bool(projectile, "AG", true);
}

bool fighter(const IniFile& rules, const WorldEntity& entity) {
    return entity.kind == WorldKind::Unit && rules.read_integer(entity.type, "Speed") > 0 &&
        !rules.read_bool(entity.type, "Naval") && !rules.read_bool(entity.type, "Harvester") &&
        !rules.read_bool(entity.type, "Weeder") && !mcv(rules, entity) && ground_weapon(rules, entity.type);
}

std::optional<std::int32_t> side_index(const IniFile& rules, const WorldHouse& house) {
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

bool owns(const Actors& buildings, std::string_view type) {
    return std::any_of(buildings.begin(), buildings.end(),
        [&](const WorldEntity* entity) { return equal_name(entity->type, type); });
}

bool prerequisite(const IniFile& rules, const Actors& buildings, const std::string& name) {
    constexpr std::array<const char*, 6> groups{{"Power", "Factory", "Barracks", "Radar", "Tech", "Proc"}};
    for (const auto* group : groups) {
        if (!equal_name(name, group)) continue;
        for (const auto& type : rules.read_list("General", std::string("Prerequisite") + group))
            if (owns(buildings, type)) return true;
        return false;
    }
    return owns(buildings, name);
}

bool buildable(const IniFile& rules, const WorldHouse& house, const Actors& actors,
               const Actors& buildings, const std::string& type, std::int64_t cash) {
    const auto cost = rules.read_integer(type, "Cost", -1);
    const auto tech = rules.read_integer(type, "TechLevel", -1);
    // TODO-ORIGINAL: This is a conservative policy filter, not House::Can_Build.
    // The production runtime remains responsible for the final legality check.
    // Prototype games use level ten when the house has no resolved tech level.
    if (cost <= 0 || cost > cash || tech < 0 || tech > house.tech_level.value_or(10) ||
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

const WorldEntity* factory(const IniFile& rules, const Actors& buildings, const char* kind) {
    const auto found = std::find_if(buildings.begin(), buildings.end(), [&](const WorldEntity* entity) {
        return equal_name(rules.read_string(entity->type, "Factory"), kind) &&
            !rules.read_bool(entity->type, "Naval");
    });
    return found == buildings.end() ? nullptr : *found;
}

skirmish::Order order(skirmish::OrderKind kind, std::uint32_t house, const WorldEntity& entity,
                      std::string type = {}) {
    skirmish::Order result;
    result.kind = kind;
    result.house = house;
    result.entity = entity.id;
    result.type = std::move(type);
    result.cell = *entity.cell;
    return result;
}

std::uint64_t distance(ra2map::Cell from, ra2map::Cell to) {
    const auto dx = static_cast<std::int64_t>(from.x) - to.x;
    const auto dy = static_cast<std::int64_t>(from.y) - to.y;
    return static_cast<std::uint64_t>(dx * dx + dy * dy);
}
} // namespace

PrototypeAI::PrototypeAI(const rules::IniFile& base_rules) : rules_(base_rules) {}

void PrototypeAI::reset() noexcept {
    last_frame_.reset();
    computer_house_ = player_house_ = 0;
    next_build_ = next_attack_ = 0;
}

std::vector<skirmish::Order> PrototypeAI::tick(const session::world::Snapshot& world,
    std::uint32_t computer_house_id, std::uint32_t player_house_id, std::int32_t game_frame) {
    std::vector<skirmish::Order> result;
    if (world.edition != session::world::Edition::Base || world.pending_cell_setup || game_frame < 0 ||
        computer_house_id == 0 || player_house_id == 0 || computer_house_id == player_house_id) return result;
    const auto find_house = [&](std::uint32_t id) {
        return std::find_if(world.houses.begin(), world.houses.end(), [&](const WorldHouse& h) { return h.id == id; });
    };
    const auto computer = find_house(computer_house_id);
    if (computer == world.houses.end() || computer->human_controlled.value_or(true) ||
        find_house(player_house_id) == world.houses.end()) return result;
    if (computer_house_ != computer_house_id || player_house_ != player_house_id ||
        (last_frame_ && game_frame < *last_frame_)) reset();
    if (last_frame_ == game_frame) return result;
    computer_house_ = computer_house_id;
    player_house_ = player_house_id;
    last_frame_ = game_frame;
    const bool build_due = game_frame >= next_build_;
    const bool attack_due = game_frame >= next_attack_;
    if (!build_due && !attack_due) return result;
    if (build_due) next_build_ = static_cast<std::int64_t>(game_frame) + build_interval_frames;
    if (attack_due) next_attack_ = static_cast<std::int64_t>(game_frame) + attack_interval_frames;

    Actors actors, buildings, enemies;
    for (const auto& entity : world.entities) {
        if (!live(entity)) continue;
        if (entity.house_id == computer_house_id) {
            actors.push_back(&entity);
            if (entity.kind == WorldKind::Structure) buildings.push_back(&entity);
        } else if (entity.house_id == player_house_id && entity.kind != WorldKind::Terrain &&
                   entity.kind != WorldKind::Aircraft && !rules_.read_bool(entity.type, "Immune") &&
                   rules_.read_bool(entity.type, "LegalTarget", true)) enemies.push_back(&entity);
    }
    const auto by_id = [](const WorldEntity* a, const WorldEntity* b) { return a->id < b->id; };
    std::sort(actors.begin(), actors.end(), by_id);
    std::sort(buildings.begin(), buildings.end(), by_id);
    std::sort(enemies.begin(), enemies.end(), by_id);
    if (actors.empty() || enemies.empty()) return result;

    if (build_due) {
        if (std::none_of(buildings.begin(), buildings.end(), [&](const WorldEntity* e) { return yard(rules_, *e); })) {
            for (const WorldEntity* entity : actors) if (mcv(rules_, *entity)) {
                result.push_back(order(skirmish::OrderKind::Deploy, computer_house_id, *entity));
                break;
            }
        }
        auto cash = static_cast<std::int64_t>(computer->cash.value_or(0));
        if (const auto* construction = factory(rules_, buildings, "BuildingType")) {
            const auto side = side_index(rules_, *computer);
            std::int64_t power = 0;
            for (const WorldEntity* building : buildings) power += rules_.read_integer(building->type, "Power");
            // TODO-ORIGINAL: Build one power plant, refinery, barracks and war
            // factory. Replace lost buildings and add power when consumption wins.
            for (const char* role : {"BuildPower", "BuildRefinery", "BuildBarracks", "BuildWeapons"}) {
                const auto types = rules_.read_list("AI", role);
                const bool have = std::any_of(types.begin(), types.end(),
                    [&](const auto& type) { return owns(buildings, type); });
                if (have && !(equal_name(role, "BuildPower") && power < 0)) continue;
                for (const auto& type : types) {
                    const auto* planning_side = rules_.find(type, "AIBasePlanningSide");
                    if (planning_side && (!side || rules_.read_integer(type, "AIBasePlanningSide", -1) != *side)) continue;
                    if (!registered(rules_, "BuildingTypes", type) ||
                        !buildable(rules_, *computer, actors, buildings, type, cash)) continue;
                    result.push_back(order(skirmish::OrderKind::Build, computer_house_id, *construction, type));
                    cash -= rules_.read_integer(type, "Cost");
                    break;
                }
                if (!result.empty() && result.back().kind == skirmish::OrderKind::Build) break;
            }
        }
        if (const auto* weapons = factory(rules_, buildings, "UnitType")) {
            std::string selected;
            auto selected_cost = std::numeric_limits<std::int32_t>::max();
            if (const auto* vehicles = rules_.find_section("VehicleTypes")) for (const auto& entry : vehicles->entries) {
                const auto& type = entry.value;
                // TODO-ORIGINAL: Prefer the cheapest available ground AFV. This
                // gives both base factions tanks without guessing type names.
                if (!equal_name(rules_.read_string(type, "Category"), "AFV") ||
                    rules_.read_integer(type, "Speed") <= 0 || rules_.read_integer(type, "Strength") <= 0 ||
                    rules_.read_bool(type, "Naval") || rules_.read_bool(type, "Harvester") ||
                    rules_.read_bool(type, "Weeder") || !rules_.read_string(type, "DeploysInto").empty() ||
                    !ground_weapon(rules_, type) || !buildable(rules_, *computer, actors, buildings, type, cash)) continue;
                const auto cost = rules_.read_integer(type, "Cost");
                if (cost < selected_cost || (cost == selected_cost && type < selected)) {
                    selected = type;
                    selected_cost = cost;
                }
            }
            if (!selected.empty()) result.push_back(order(skirmish::OrderKind::Build, computer_house_id, *weapons, selected));
        }
    }

    if (attack_due) for (const WorldEntity* entity : actors) {
        if (!fighter(rules_, *entity)) continue;
        const WorldEntity* target = nullptr;
        auto nearest = std::numeric_limits<std::uint64_t>::max();
        for (const WorldEntity* enemy : enemies) {
            const auto range = distance(*entity->cell, *enemy->cell);
            if (range < nearest) { target = enemy; nearest = range; }
        }
        if (target) {
            auto attack = order(skirmish::OrderKind::Attack, computer_house_id, *entity);
            attack.target = target->id;
            attack.cell = *target->cell;
            result.push_back(std::move(attack));
        }
    }
    return result;
}

} // namespace ra2::sim
