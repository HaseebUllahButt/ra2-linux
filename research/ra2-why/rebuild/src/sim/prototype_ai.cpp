#include "prototype_ai.h"
#include "../loop/profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
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

const WorldEntity* factory(const IniFile& rules, const Actors& buildings, const char* kind, bool naval = false) {
    const auto found = std::find_if(buildings.begin(), buildings.end(), [&](const WorldEntity* entity) {
        return equal_name(rules.read_string(entity->type, "Factory"), kind) &&
            rules.read_bool(entity->type, "Naval") == naval;
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


namespace {
using Trigger = PrototypeAI::Trigger;

// AI.INI lines keep empty fields, so IniFile::split_list does not fit here.
std::vector<std::string> fields(std::string_view value) {
    std::vector<std::string> result;
    for (;;) {
        const auto comma = value.find(',');
        result.emplace_back(IniFile::trim(value.substr(0, comma)));
        if (comma == std::string_view::npos) return result;
        value.remove_prefix(comma + 1);
    }
}

std::int32_t number(const std::string& text, std::int32_t fallback = 0) {
    return IniFile::integer_value(text).value_or(fallback);
}

// The 64 hex digits store four {int, comparator} pairs as little-endian bytes.
// The original reads only the first pair (TSpp AITriggerTypeClass::Data).
std::int32_t hex_word(const std::string& text, std::size_t word) {
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
bool compare(std::int64_t value, std::int32_t operand, std::int32_t comparator) {
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

bool none(std::string_view name) { return name.empty() || equal_name(name, "<none>"); }

// Rules lists such as TeamDelays and TotalAITeamCap are hardest first.
std::int32_t by_difficulty(const IniFile& rules, const char* key, PrototypeAI::Difficulty level,
                           std::int32_t fallback) {
    const auto values = rules.read_list("General", key);
    const auto index = static_cast<std::size_t>(2 - static_cast<std::int32_t>(level));
    return index < values.size() ? number(values[index], fallback) : fallback;
}

// ScriptTypes quarry numbers, as used by the map trigger prototype.
bool quarry(const IniFile& rules, std::int32_t kind, const WorldEntity& entity) {
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

const char* factory_kind(const IniFile& rules, const std::string& type) {
    if (registered(rules, "InfantryTypes", type)) return "InfantryType";
    if (registered(rules, "VehicleTypes", type)) return "UnitType";
    if (registered(rules, "AircraftTypes", type)) return "AircraftType";
    return nullptr;
}

std::uint32_t chebyshev(ra2map::Cell a, ra2map::Cell b) {
    const auto dx = std::abs(static_cast<std::int32_t>(a.x) - b.x);
    const auto dy = std::abs(static_cast<std::int32_t>(a.y) - b.y);
    return static_cast<std::uint32_t>(std::max(dx, dy));
}

constexpr std::int32_t frames_per_second = 15;
constexpr std::int32_t move_timeout_frames = 1800;
constexpr std::int64_t unlimited_cash = std::numeric_limits<std::int32_t>::max();
} // namespace

struct PrototypeAI::Context {
    const session::world::Snapshot& world;
    const WorldHouse& computer;
    const WorldHouse& player;
    std::int32_t frame = 0;
    std::optional<std::int32_t> side;
    Actors actors, buildings, enemies;
    std::int64_t cash = 0;
    std::map<std::uint32_t, const WorldEntity*> own; // live own actors by ID
    std::map<std::uint32_t, const WorldEntity*> targets; // live legal enemy targets

    std::int64_t count(std::uint32_t house, const std::string& type) const {
        return std::count_if(world.entities.begin(), world.entities.end(), [&](const WorldEntity& e) {
            return e.house_id == house && live(e) && equal_name(e.type, type);
        });
    }
};

PrototypeAI::PrototypeAI(const rules::IniFile& base_rules, const rules::IniFile* base_ai, Difficulty difficulty)
    : rules_(base_rules), difficulty_(difficulty) {
    if (!base_ai) return;
    const auto& ai = *base_ai;
    // Section bodies keyed by the ID lists, as AI.INI stores them.
    const auto ids = [&](const char* list) {
        std::vector<std::string> result;
        if (const auto* section = ai.find_section(list))
            for (const auto& entry : section->entries) if (!entry.value.empty()) result.push_back(entry.value);
        return result;
    };
    for (const auto& id : ids("TaskForces")) {
        TaskForce force{id, {}};
        // TODO-ORIGINAL: TaskForceClass holds six entries in this prototype.
        for (int i = 0; i < 6; ++i) {
            const auto parts = fields(ai.read_string(id, std::to_string(i)));
            if (parts.size() >= 2 && number(parts[0]) > 0 && !parts[1].empty())
                force.members.emplace_back(number(parts[0]), parts[1]);
        }
        if (!force.members.empty()) task_forces_.push_back(std::move(force));
    }
    for (const auto& id : ids("ScriptTypes")) {
        std::vector<ScriptStep> steps;
        for (int i = 0; i < 50; ++i) {
            const auto* line = ai.find(id, std::to_string(i));
            if (!line) break;
            const auto parts = fields(line->value);
            steps.push_back({number(parts[0], -1), parts.size() > 1 ? number(parts[1]) : 0});
        }
        script_ids_.push_back(id);
        scripts_.push_back(std::move(steps));
    }
    for (const auto& id : ids("TeamTypes")) {
        TeamType team;
        team.id = id;
        team.task_force = ai.read_string(id, "TaskForce");
        team.script = ai.read_string(id, "Script");
        team.max = ai.read_integer(id, "Max", 1);
        team.tech_level = ai.read_integer(id, "TechLevel");
        team.base_defense = ai.read_bool(id, "IsBaseDefense");
        team_types_.push_back(std::move(team));
    }
    const auto has_team = [&](const std::string& id) {
        return std::any_of(team_types_.begin(), team_types_.end(), [&](const TeamType& t) { return t.id == id; });
    };
    if (const auto* section = ai.find_section("AITriggerTypes")) for (const auto& entry : section->entries) {
        // ID=Name,Team1,House,TechLevel,Condition,Object,Data,Weight,Min,Max,
        //    Skirmish,Unused,Side,BaseDefense,Team2,Easy,Normal,Hard
        const auto f = fields(entry.value);
        if (f.size() < 18 || !has_team(f[1])) continue;
        Trigger trigger;
        trigger.id = entry.key;
        trigger.name = f[0];
        trigger.team1 = f[1];
        trigger.house = f[2];
        trigger.tech_level = number(f[3]);
        trigger.condition = number(f[4], -1);
        trigger.condition_object = none(f[5]) ? std::string{} : f[5];
        trigger.number = hex_word(f[6], 0);
        trigger.comparator = hex_word(f[6], 1);
        trigger.weight = IniFile::double_value(f[7]).value_or(0);
        trigger.minimum = IniFile::double_value(f[8]).value_or(0);
        trigger.maximum = IniFile::double_value(f[9]).value_or(0);
        trigger.skirmish = number(f[10]) != 0;
        trigger.side = number(f[12]);
        trigger.base_defense = number(f[13]) != 0;
        trigger.team2 = has_team(f[14]) ? f[14] : std::string{};
        trigger.easy = number(f[15]) != 0;
        trigger.normal = number(f[16]) != 0;
        trigger.hard = number(f[17]) != 0;
        triggers_.push_back(std::move(trigger));
    }
    reset();
}

void PrototypeAI::reset() noexcept {
    last_frame_.reset();
    computer_house_ = player_house_ = 0;
    next_build_ = next_attack_ = next_team_ = 0;
    teams_.clear();
    weights_.clear();
    for (const auto& trigger : triggers_) weights_.push_back(trigger.weight);
    seed_ = 1;
}

std::optional<double> PrototypeAI::trigger_weight(const std::string& id) const {
    for (std::size_t i = 0; i < triggers_.size(); ++i) if (triggers_[i].id == id) return weights_[i];
    return {};
}

std::vector<PrototypeAI::TeamView> PrototypeAI::teams() const {
    std::vector<TeamView> result;
    for (const auto& team : teams_)
        result.push_back({team_types_[team.type].id, triggers_[team.trigger].id, team.members, team.forming, team.line});
    return result;
}

// TODO-ORIGINAL: A private linear congruential stream keeps the shared world
// random state untouched. The original draws from the scenario Randomizer.
std::uint32_t PrototypeAI::random(std::uint32_t low, std::uint32_t high) {
    seed_ = seed_ * 1103515245u + 12345u;
    return low + (seed_ >> 8) % (high - low + 1);
}

bool PrototypeAI::condition(const Context& context, const Trigger& trigger) const {
    const auto& object = trigger.condition_object;
    switch (trigger.condition) {
    case -1: return true;
    case 0: return !object.empty() && compare(context.count(context.player.id, object), trigger.number, trigger.comparator);
    case 1: return !object.empty() && compare(context.count(context.computer.id, object), trigger.number, trigger.comparator);
    case 4: return compare(context.player.cash.value_or(0), trigger.number, trigger.comparator);
    case 7: {
        if (object.empty()) return false;
        std::int64_t total = 0;
        // TODO-ORIGINAL: Civilian houses are the [Sides] entry that holds Neutral.
        for (const auto& house : context.world.houses)
            if (equal_name(house.country, "Neutral") || equal_name(house.country, "Special"))
                total += context.count(house.id, object);
        return compare(total, trigger.number, trigger.comparator);
    }
    // TODO-ORIGINAL: 2/3 enemy power state and 5/6 super weapon charge have no
    // runtime state in the native match yet, so they never match.
    default: return false;
    }
}

void PrototypeAI::select_team(const Context& context) {
    const auto cap = by_difficulty(rules_, "TotalAITeamCap", difficulty_, 30);
    const auto max_defense = by_difficulty(rules_, "MaximumAIDefensiveTeams", difficulty_, 2);
    const auto total = static_cast<std::int32_t>(teams_.size());
    const auto defensive = static_cast<std::int32_t>(std::count_if(teams_.begin(), teams_.end(),
        [&](const Team& team) { return team_types_[team.type].base_defense; }));
    // Shape of the recovered team picker (HouseAI 006F0AB0 in the YR map;
    // base RA2 address is not named yet). At the cap the original disbands one
    // defensive team. TODO-ORIGINAL: this prototype waits instead.
    if (total >= cap) return;
    const bool enough_defense = max_defense < defensive;
    const auto type_index = [&](const std::string& id) {
        for (std::size_t i = 0; i < team_types_.size(); ++i) if (team_types_[i].id == id) return i;
        return team_types_.size();
    };
    const auto team_ready = [&](std::size_t type) {
        const auto& team = team_types_[type];
        const auto existing = std::count_if(teams_.begin(), teams_.end(), [&](const Team& t) { return t.type == type; });
        if (existing >= team.max) return false;
        const auto force = std::find_if(task_forces_.begin(), task_forces_.end(),
            [&](const TaskForce& f) { return f.id == team.task_force; });
        if (force == task_forces_.end()) return false;
        // Each member type must be buildable now (cash aside) or recruitable.
        for (const auto& [count, type_name] : force->members) {
            (void)count;
            const bool idle = std::any_of(context.actors.begin(), context.actors.end(),
                [&](const WorldEntity* e) { return e->kind != WorldKind::Structure && equal_name(e->type, type_name); });
            const auto* kind = factory_kind(rules_, type_name);
            const bool naval = rules_.read_bool(type_name, "Naval");
            if (!idle && !(kind && factory(rules_, context.buildings, kind, naval) &&
                buildable(rules_, context.computer, context.actors, context.buildings, type_name, unlimited_cash)))
                return false;
        }
        return true;
    };
    std::vector<std::pair<std::size_t, std::int64_t>> choices;
    std::int64_t sum = 0;
    bool exclusive = false;
    for (std::size_t i = 0; i < triggers_.size(); ++i) {
        const auto& trigger = triggers_[i];
        const bool enabled = difficulty_ == Difficulty::Easy ? trigger.easy :
            difficulty_ == Difficulty::Normal ? trigger.normal : trigger.hard;
        if (!trigger.skirmish || !enabled || (trigger.base_defense && enough_defense)) continue;
        if (!equal_name(trigger.house, "<all>") && !equal_name(trigger.house, context.computer.country)) continue;
        if (trigger.side != 0 && (!context.side || *context.side + 1 != trigger.side)) continue;
        if (trigger.tech_level > context.computer.tech_level.value_or(10)) continue;
        const auto first = type_index(trigger.team1);
        if (first == team_types_.size() || !team_ready(first) || !condition(context, trigger)) continue;
        // Weight 5000 marks urgent triggers. Once one matches, only those count.
        const auto weight = static_cast<std::int64_t>(weights_[i]);
        if (weight == 5000 && !exclusive) { exclusive = true; choices.clear(); sum = 0; }
        else if (exclusive && weight != 5000) continue;
        if (weight <= 0) continue;
        choices.emplace_back(i, weight);
        sum += weight;
    }
    if (choices.empty()) return;
    const auto roll = random(1, static_cast<std::uint32_t>(std::min<std::int64_t>(sum, 0x7fffffff)));
    std::int64_t walk = 0;
    std::size_t picked = choices.back().first;
    for (const auto& [index, weight] : choices) {
        walk += weight;
        if (roll <= walk) { picked = index; break; }
    }
    std::vector<std::size_t> types{type_index(triggers_[picked].team1)};
    if (!triggers_[picked].team2.empty()) {
        const auto second = type_index(triggers_[picked].team2);
        if (second != team_types_.size() && team_ready(second)) types.push_back(second);
    }
    // A forming team of the same type blocks the pick, as in the original.
    for (const auto& team : teams_)
        if (team.forming && std::find(types.begin(), types.end(), team.type) != types.end()) return;
    for (const auto type : types) {
        Team team;
        team.type = type;
        team.trigger = picked;
        teams_.push_back(std::move(team));
    }
}

void PrototypeAI::fill_teams(Context& context, std::vector<skirmish::Order>* result) {
    // Recruit free units first, in ascending ID order.
    std::vector<std::uint32_t> taken;
    for (const auto& team : teams_) taken.insert(taken.end(), team.members.begin(), team.members.end());
    std::vector<std::string> wanted; // missing member types in team order
    for (auto& team : teams_) {
        if (!team.forming) continue;
        const auto& type = team_types_[team.type];
        const auto force = std::find_if(task_forces_.begin(), task_forces_.end(),
            [&](const TaskForce& f) { return f.id == type.task_force; });
        if (force == task_forces_.end()) continue;
        bool full = true;
        for (const auto& [count, name] : force->members) {
            auto have = std::count_if(team.members.begin(), team.members.end(), [&](std::uint32_t id) {
                const auto found = context.own.find(id);
                return found != context.own.end() && equal_name(found->second->type, name);
            });
            for (const WorldEntity* entity : context.actors) {
                if (have >= count) break;
                if (entity->kind == WorldKind::Structure || !equal_name(entity->type, name) ||
                    std::find(taken.begin(), taken.end(), entity->id) != taken.end()) continue;
                team.members.push_back(entity->id);
                taken.push_back(entity->id);
                ++have;
            }
            if (have < count) { full = false; wanted.push_back(name); }
        }
        std::sort(team.members.begin(), team.members.end());
        if (full) { team.forming = false; team.line = 0; team.wait_until = context.frame; }
    }
    // One legal request per factory per build decision. Production decides the rest.
    if (!result) return;
    std::vector<std::uint32_t> busy;
    for (const auto& name : wanted) {
        const auto* kind = factory_kind(rules_, name);
        if (!kind) continue;
        const auto* plant = factory(rules_, context.buildings, kind, rules_.read_bool(name, "Naval"));
        if (!plant || std::find(busy.begin(), busy.end(), plant->id) != busy.end() ||
            !buildable(rules_, context.computer, context.actors, context.buildings, name, context.cash)) continue;
        result->push_back(order(skirmish::OrderKind::Build, context.computer.id, *plant, name));
        context.cash -= rules_.read_integer(name, "Cost");
        busy.push_back(plant->id);
    }
}

void PrototypeAI::finish(Team& team, bool success) {
    const auto& trigger = triggers_[team.trigger];
    auto& weight = weights_[team.trigger];
    // Rules AITriggerSuccessWeightDelta / AITriggerFailureWeightDelta (signed).
    weight += rules_.read_double("General", success ? "AITriggerSuccessWeightDelta" : "AITriggerFailureWeightDelta",
        success ? 20.0 : -50.0);
    weight = std::max(trigger.minimum, std::min(trigger.maximum, weight));
    team.members.clear();
    team.forming = false;
    team.guarding = false;
    team.line = std::numeric_limits<std::int32_t>::max();
}

void PrototypeAI::run_teams(Context& context, std::vector<skirmish::Order>& result) {
    const auto& building_list = [&]() {
        std::vector<std::string> names;
        if (const auto* section = rules_.find_section("BuildingTypes"))
            for (const auto& entry : section->entries) names.push_back(entry.value);
        return names;
    }();
    for (auto& team : teams_) {
        if (team.forming || team.guarding || team.members.empty() || context.frame < team.wait_until) continue;
        const auto script_at = std::find(script_ids_.begin(), script_ids_.end(), team_types_[team.type].script);
        if (script_at == script_ids_.end()) { finish(team, false); continue; }
        const auto& steps = scripts_[static_cast<std::size_t>(script_at - script_ids_.begin())];
        const auto centre = *context.own.at(team.members.front())->cell;
        const auto send = [&](skirmish::OrderKind kind, std::uint32_t target, ra2map::Cell cell) {
            for (const auto id : team.members) {
                auto item = order(kind, context.computer.id, *context.own.at(id));
                item.target = target;
                item.cell = cell;
                result.push_back(std::move(item));
            }
        };
        const auto nearest = [&](const std::map<std::uint32_t, const WorldEntity*>& pool, auto&& match) {
            const WorldEntity* best = nullptr;
            auto range = std::numeric_limits<std::uint64_t>::max();
            for (const auto& [id, entity] : pool) {
                (void)id;
                if (!match(*entity)) continue;
                const auto d = distance(centre, *entity->cell);
                if (d < range) { best = entity; range = d; }
            }
            return best;
        };
        const auto building_named = [&](std::int32_t argument) {
            const auto index = static_cast<std::size_t>(argument & 0xffff);
            return index < building_list.size() ? building_list[index] : std::string{};
        };
        for (int guard = 0; guard < 16; ++guard) {
            if (team.line < 0 || team.line >= static_cast<std::int32_t>(steps.size())) { finish(team, true); break; }
            const auto step = steps[static_cast<std::size_t>(team.line)];
            bool yield = false;
            switch (step.action) {
            case 0: { // Attack quarry until none of that kind is left.
                if (team.target && !context.targets.count(team.target)) team.target = 0;
                if (!team.target) {
                    const auto* target = nearest(context.targets,
                        [&](const WorldEntity& e) { return quarry(rules_, step.argument, e); });
                    if (!target) { ++team.line; break; }
                    team.target = target->id;
                }
                send(skirmish::OrderKind::Attack, team.target, *context.targets.at(team.target)->cell);
                yield = true;
                break;
            }
            case 46: { // Attack one enemy structure of a [BuildingTypes] index.
                if (team.target && !context.targets.count(team.target)) { team.target = 0; ++team.line; break; }
                if (!team.target) {
                    const auto name = building_named(step.argument);
                    const auto* target = nearest(context.targets, [&](const WorldEntity& e) {
                        return e.kind == WorldKind::Structure && equal_name(e.type, name);
                    });
                    if (!target) { ++team.line; break; }
                    team.target = target->id;
                }
                send(skirmish::OrderKind::Attack, team.target, *context.targets.at(team.target)->cell);
                yield = true;
                break;
            }
            case 47: case 58: { // Move to an enemy (47) or own (58) structure type.
                if (!team.moving) {
                    const auto name = building_named(step.argument);
                    const auto& pool = step.action == 47 ? context.targets : context.own;
                    const auto* goal = nearest(pool, [&](const WorldEntity& e) {
                        return e.kind == WorldKind::Structure && equal_name(e.type, name);
                    });
                    if (!goal) { ++team.line; break; }
                    team.goal = *goal->cell;
                    team.moving = true;
                    team.deadline = context.frame + move_timeout_frames;
                    send(skirmish::OrderKind::Move, 0, team.goal);
                    yield = true;
                    break;
                }
                // TODO-ORIGINAL: arrival uses a three-cell box, not TeamClass coordinates.
                const bool arrived = std::all_of(team.members.begin(), team.members.end(), [&](std::uint32_t id) {
                    return chebyshev(*context.own.at(id)->cell, team.goal) <= 3;
                });
                if (arrived || context.frame >= team.deadline) { team.moving = false; ++team.line; break; }
                yield = true;
                break;
            }
            case 5: // Guard. TODO-ORIGINAL: argument units are taken as seconds.
                if (!team.moving) {
                    team.moving = true;
                    team.wait_until = context.frame + std::max(step.argument, 1) * frames_per_second;
                    yield = true;
                } else { team.moving = false; ++team.line; }
                break;
            case 6: team.line = step.argument - 1; break; // Jump; lines count from one.
            case 11: team.guarding = true; yield = true; break; // Take a new mission and hold.
            default:
                // TODO-ORIGINAL: 49/53/54 regroup and gather, transport, super
                // weapon and the other ScriptClass actions are skipped.
                ++team.line;
                break;
            }
            if (yield || team.line == std::numeric_limits<std::int32_t>::max()) break;
        }
    }
    teams_.erase(std::remove_if(teams_.begin(), teams_.end(),
        [](const Team& team) { return !team.forming && team.members.empty(); }), teams_.end());
}

std::vector<skirmish::Order> PrototypeAI::tick(const session::world::Snapshot& world,
    std::uint32_t computer_house_id, std::uint32_t player_house_id, std::int32_t game_frame) {
    loop::profile::Scope probe(loop::profile::Phase::AI);
    std::vector<skirmish::Order> result;
    if (world.edition != session::world::Edition::Base || world.pending_cell_setup || game_frame < 0 ||
        computer_house_id == 0 || player_house_id == 0 || computer_house_id == player_house_id) return result;
    const auto find_house = [&](std::uint32_t id) {
        return std::find_if(world.houses.begin(), world.houses.end(), [&](const WorldHouse& h) { return h.id == id; });
    };
    const auto computer = find_house(computer_house_id);
    const auto player = find_house(player_house_id);
    if (computer == world.houses.end() || computer->human_controlled.value_or(true) ||
        player == world.houses.end()) return result;
    if (computer_house_ != computer_house_id || player_house_ != player_house_id ||
        (last_frame_ && game_frame < *last_frame_)) reset();
    if (last_frame_ == game_frame) return result;
    if (computer_house_ != computer_house_id) seed_ = 0x9e3779b9u ^ computer_house_id;
    computer_house_ = computer_house_id;
    player_house_ = player_house_id;
    last_frame_ = game_frame;
    const bool teams_on = uses_ai_ini();
    const bool build_due = game_frame >= next_build_;
    const bool attack_due = game_frame >= next_attack_;
    const bool team_due = teams_on && game_frame >= next_team_;
    if (!build_due && !attack_due && !team_due) return result;
    if (build_due) next_build_ = static_cast<std::int64_t>(game_frame) + build_interval_frames;
    if (attack_due) next_attack_ = static_cast<std::int64_t>(game_frame) + attack_interval_frames;
    if (team_due) next_team_ = static_cast<std::int64_t>(game_frame) +
        std::max(1, by_difficulty(rules_, "TeamDelays", difficulty_, 2000));

    Context context{world, *computer, *player, game_frame, side_index(rules_, *computer), {}, {}, {}, 0, {}, {}};
    auto& actors = context.actors;
    auto& buildings = context.buildings;
    auto& enemies = context.enemies;
    for (const auto& entity : world.entities) {
        if (!live(entity)) continue;
        if (entity.house_id == computer_house_id) {
            actors.push_back(&entity);
            context.own[entity.id] = &entity;
            if (entity.kind == WorldKind::Structure) buildings.push_back(&entity);
        } else if (entity.house_id == player_house_id && entity.kind != WorldKind::Terrain &&
                   entity.kind != WorldKind::Aircraft && !rules_.read_bool(entity.type, "Immune") &&
                   rules_.read_bool(entity.type, "LegalTarget", true)) {
            enemies.push_back(&entity);
            context.targets[entity.id] = &entity;
        }
    }
    const auto by_id = [](const WorldEntity* a, const WorldEntity* b) { return a->id < b->id; };
    std::sort(actors.begin(), actors.end(), by_id);
    std::sort(buildings.begin(), buildings.end(), by_id);
    std::sort(enemies.begin(), enemies.end(), by_id);
    if (actors.empty() || enemies.empty()) return result;
    context.cash = static_cast<std::int64_t>(computer->cash.value_or(0));

    // Drop dead members. A launched team with nobody left has failed.
    for (auto& team : teams_) {
        team.members.erase(std::remove_if(team.members.begin(), team.members.end(),
            [&](std::uint32_t id) { return !context.own.count(id); }), team.members.end());
        if (!team.forming && team.members.empty()) finish(team, false);
    }
    teams_.erase(std::remove_if(teams_.begin(), teams_.end(),
        [](const Team& team) { return !team.forming && team.members.empty(); }), teams_.end());
    if (team_due) select_team(context);

    if (build_due) {
        if (std::none_of(buildings.begin(), buildings.end(), [&](const WorldEntity* e) { return yard(rules_, *e); })) {
            for (const WorldEntity* entity : actors) if (mcv(rules_, *entity)) {
                result.push_back(order(skirmish::OrderKind::Deploy, computer_house_id, *entity));
                break;
            }
        }
        auto& cash = context.cash;
        if (const auto* construction = factory(rules_, buildings, "BuildingType")) {
            const auto& side = context.side;
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
        if (teams_on) fill_teams(context, &result);
        else if (const auto* weapons = factory(rules_, buildings, "UnitType")) {
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
    } else if (teams_on) {
        fill_teams(context, nullptr); // Recruit finished products between build decisions.
    }

    if (attack_due && teams_on) run_teams(context, result);
    else if (attack_due) for (const WorldEntity* entity : actors) {
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
