#include "prototype_ai_internal.h"
#include "../loop/profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

namespace ra2::sim {
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
        // TaskForceClass holds six entries; Team +84 counts six types.
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
        team.priority = ai.read_integer(id, "Priority", 5);
        team.base_defense = ai.read_bool(id, "IsBaseDefense");
        team.guard_slower = ai.read_bool(id, "GuardSlower");
        team.recruiter = ai.read_bool(id, "Recruiter");
        team.autocreate = ai.read_bool(id, "Autocreate");
        team.prebuild = ai.read_bool(id, "Prebuild");
        team.reinforce = ai.read_bool(id, "Reinforce");
        team.whiner = ai.read_bool(id, "Whiner");
        team.aggressive = ai.read_bool(id, "Aggressive");
        team.loose_recruit = ai.read_bool(id, "LooseRecruit");
        team.suicide = ai.read_bool(id, "Suicide");
        team.annoyance = ai.read_bool(id, "Annoyance");
        team.recruitable = ai.read_bool(id, "AreTeamMembersRecruitable", true);
        team.only_target_house_enemy = ai.read_bool(id, "OnlyTargetHouseEnemy");
        team_types_.push_back(std::move(team));
    }
    const auto has_team = [&](const std::string& id) {
        return std::any_of(team_types_.begin(), team_types_.end(), [&](const TeamType& t) { return t.id == id; });
    };
    for (const auto& team : team_types_) authored_autocreate_.push_back(team.autocreate);
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
    members_.clear();
    ai_started_ = false;
    choice_ = {};
    choice_product_ = {};
    base_centres_.clear();
    plan_.clear();
    naval_ = true; // 004E18C0
    weights_.clear();
    for (const auto& trigger : triggers_) weights_.push_back(trigger.weight);
    for (std::size_t i = 0; i < team_types_.size() && i < authored_autocreate_.size(); ++i)
        team_types_[i].autocreate = authored_autocreate_[i];
    rng_ = Random(1);
}

void PrototypeAI::placement_failed(std::uint32_t house, const std::string& type) {
    // 0044B480: Exit_Object returned 0 for a product whose type is a
    // building (What_Am_I 6) with Naval (TechnoType +ADA) set. A failed
    // land building only loses its product; its node stays, so 004E9660
    // picks it again.
    if (house == computer_house_ && registered(rules_, "BuildingTypes", type) && rules_.read_bool(type, "Naval"))
        naval_ = false;
}

std::int32_t PrototypeAI::first_team_frame(const rules::IniFile& rules, Difficulty difficulty,
                                           std::int32_t house_index) {
    return std::max(0, by_difficulty(rules, "TeamDelays", difficulty, 2000)) + std::max(0, house_index) * 0xaf;
}

std::optional<double> PrototypeAI::trigger_weight(const std::string& id) const {
    for (std::size_t i = 0; i < triggers_.size(); ++i) if (triggers_[i].id == id) return weights_[i];
    return {};
}

std::vector<PrototypeAI::TeamView> PrototypeAI::teams() const {
    std::vector<TeamView> result;
    for (const auto& team : teams_)
        result.push_back({team_types_[team.type].id, team.trigger < triggers_.size() ? triggers_[team.trigger].id : std::string{},
            team.members, !team.moving, team.line, team.queued.object, team.queued.cell});
    return result;
}

// The original Random_Pick (006388A0) on a private stream. TODO-ORIGINAL:
// the original draws from the shared scenario Randomizer; this stream keeps
// the shared world random state untouched.
std::uint32_t PrototypeAI::random(std::uint32_t low, std::uint32_t high) {
    return static_cast<std::uint32_t>(rng_.ranged(static_cast<std::int32_t>(low), static_cast<std::int32_t>(high)));
}

namespace {
// BuildingType gates shared by 004EF890 and 004EF600: OwnerFlags,
// RequiredHouses, ForbiddenHouses and AIBasePlanningSide. With the Super
// Weapons option (00A3D2C3) off, a type whose SuperWeapon (BuildingType
// +1374) has DisableableFromShell (SuperWeaponType +DF, through the
// house super weapon at +1A0) is left out.
bool plan_owner(const IniFile& rules, const std::string& type, const WorldHouse& house,
                std::optional<std::int32_t> side, bool super_weapons) {
    if (!contains(rules.read_list(type, "Owner"), house.country)) return false;
    const auto required = rules.read_list(type, "RequiredHouses");
    if ((!required.empty() && !contains(required, house.country)) ||
        contains(rules.read_list(type, "ForbiddenHouses"), house.country)) return false;
    const auto planning = rules.read_integer(type, "AIBasePlanningSide", -1);
    if (planning != -1 && (!side || planning != *side)) return false;
    const auto weapon = rules.read_string(type, "SuperWeapon");
    return super_weapons || none(weapon) || !rules.read_bool(weapon, "DisableableFromShell");
}

// Base 004EF600: the first entry of an [AI] list that the house may own.
std::string first_owned(const IniFile& rules, const char* list, const WorldHouse& house,
                        std::optional<std::int32_t> side, bool super_weapons) {
    for (const auto& type : rules.read_list("AI", list))
        if (registered(rules, "BuildingTypes", type) && plan_owner(rules, type, house, side, super_weapons)) return type;
    return {};
}

// Base 004EF740: every prerequisite is in the first `count` names. The
// groups map to [AI] lists: POWER BuildPower, FACTORY BuildWeapons,
// BARRACKS BuildBarracks, RADAR BuildRadar, TECH BuildTech, PROC BuildRefinery.
bool plan_prerequisites(const IniFile& rules, const std::string& type,
                        const std::vector<std::string>& names, std::size_t count) {
    const auto have = [&](std::string_view name) {
        return std::any_of(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(count),
            [&](const std::string& item) { return equal_name(item, name); });
    };
    constexpr std::array<std::pair<const char*, const char*>, 6> groups{{{"POWER", "BuildPower"},
        {"FACTORY", "BuildWeapons"}, {"BARRACKS", "BuildBarracks"}, {"RADAR", "BuildRadar"},
        {"TECH", "BuildTech"}, {"PROC", "BuildRefinery"}}};
    for (const auto& name : rules.read_list(type, "Prerequisite")) {
        const auto group = std::find_if(groups.begin(), groups.end(),
            [&](const auto& g) { return equal_name(name, g.first); });
        if (group == groups.end()) { if (!have(name)) return false; continue; }
        const auto list = rules.read_list("AI", group->second);
        if (std::none_of(list.begin(), list.end(), [&](const std::string& item) { return have(item); })) return false;
    }
    return true;
}
} // namespace

void PrototypeAI::make_plan(const Context& context) {
    const auto& house = context.computer;
    const auto tech = house.tech_level.value_or(10);
    std::vector<std::string> pool; // 004EF890 list A, in BuildingTypes order
    if (const auto* registry = rules_.find_section("BuildingTypes"))
        for (const auto& entry : registry->entries) {
            const auto& type = entry.value;
            if (plan_owner(rules_, type, house, context.side, super_weapons_) && rules_.read_bool(type, "AIBuildThis") &&
                rules_.read_integer(type, "TechLevel", -1) <= tech) pool.push_back(type);
        }
    std::vector<bool> used(pool.size(), false);
    std::vector<std::string> plan;
    const auto index = [&](const std::string& type) {
        for (std::size_t i = 0; i < pool.size(); ++i) if (equal_name(pool[i], type)) return i;
        return pool.size();
    };
    // The yard (if a candidate), then BuildPower without the candidate test.
    if (const auto at = index(first_owned(rules_, "BuildConst", house, context.side, super_weapons_)); at < pool.size()) {
        used[at] = true;
        plan.push_back(pool[at]);
    }
    if (auto power = first_owned(rules_, "BuildPower", house, context.side, super_weapons_); !power.empty()) plan.push_back(power);
    // BuildBarracks moves to the front of A and BuildWeapons to the second place.
    for (const auto& [list, slot] : {std::pair<const char*, std::size_t>{"BuildBarracks", 0}, {"BuildWeapons", 1}}) {
        const auto at = index(first_owned(rules_, list, house, context.side, super_weapons_));
        if (at < pool.size() && slot < pool.size()) {
            std::swap(pool[slot], pool[at]);
            const bool flag = used[slot];
            used[slot] = used[at];
            used[at] = flag;
        }
    }
    // Each pass adds every unused candidate whose prerequisites appeared in
    // earlier passes. Candidates that never qualify are left out.
    for (auto remaining = static_cast<std::int64_t>(pool.size()) - 1; remaining > 0;) {
        const auto before = plan.size();
        bool added = false;
        for (std::size_t i = 0; i < pool.size(); ++i) {
            if (used[i] || equal_name(pool[i], "GAPLUG") || !plan_prerequisites(rules_, pool[i], plan, before)) continue;
            used[i] = true;
            plan.push_back(pool[i]);
            added = true;
            --remaining;
        }
        if (!added) break;
    }
    // 2 - AIDifficulty (House +C8, 0 is Brutal) more refineries, each after
    // a random node from the first refinery on.
    const auto refinery = first_owned(rules_, "BuildRefinery", house, context.side, super_weapons_);
    for (std::size_t first = 0; !refinery.empty() && first + 1 < plan.size(); ++first) {
        if (!equal_name(plan[first], refinery)) continue;
        for (auto extra = static_cast<std::int32_t>(difficulty_); extra > 0; --extra) {
            const auto at = random(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(plan.size() - 1));
            plan.insert(plan.begin() + at + 1, refinery);
        }
        break;
    }
    // Rules AlliedBaseDefenseCounts or SovietBaseDefenseCounts (side 1),
    // hardest first: each defense node goes after a random node from the
    // fourth on.
    const auto defenses = by_difficulty(rules_, context.side == 1 ? "SovietBaseDefenseCounts" :
        "AlliedBaseDefenseCounts", difficulty_, 0);
    for (std::int32_t i = 0; i < defenses && plan.size() >= 4; ++i) {
        const auto at = random(3, static_cast<std::uint32_t>(plan.size() - 1));
        plan.insert(plan.begin() + at + 1, std::string{});
    }
    plan_ = std::move(plan);
}

// Base 0045EAA0, for an IsBaseDefense BuildingType (+138A): from the
// Primary weapon, rate = (int)(Damage / (ROF * 0.025)) (0079D2E8). With
// an AG projectile (+274) the ground value (+11D8) is rate * Verses[3];
// with an AA projectile (+275) the air value (+11DC) is rate * Verses[4]
// and the naval value (+11E0) is rate * Verses[0]. Each value is at most
// [AI] MaximumBaseDefenseValue (Rules +9CC).
std::array<std::int32_t, 3> PrototypeAI::defense_values(const std::string& type) const {
    std::array<std::int32_t, 3> values{};
    if (!rules_.read_bool(type, "IsBaseDefense")) return values;
    const auto weapon = rules_.read_string(type, "Primary");
    if (none(weapon) || !rules_.find_section(weapon)) return values;
    const auto rof = rules_.read_integer(weapon, "ROF");
    if (rof <= 0) return values; // TODO-ORIGINAL: the original divides by zero here.
    const double damage = rules_.read_integer(weapon, "Damage");
    const auto rate = static_cast<std::int32_t>(damage / (static_cast<double>(rof) * 0.025));
    const auto projectile = rules_.read_string(weapon, "Projectile");
    const auto verses = IniFile::split_list(rules_.read_string(rules_.read_string(weapon, "Warhead"), "Verses"));
    const auto versus = [&](std::size_t armor) {
        if (armor >= verses.size()) return 1.0;
        auto text = verses[armor];
        if (!text.empty() && text.back() == '%') text.pop_back();
        return IniFile::double_value(text).value_or(100.0) / 100.0;
    };
    const auto cap = rules_.read_integer("AI", "MaximumBaseDefenseValue", -1);
    const auto value = [&](double product) {
        return static_cast<double>(cap) < product ? cap : static_cast<std::int32_t>(product);
    };
    if (rules_.read_bool(projectile, "AG", true)) values[0] = value(rate * versus(3));
    if (rules_.read_bool(projectile, "AA", false)) {
        values[1] = value(rate * versus(4));
        values[2] = value(rate * versus(0));
    }
    return values;
}

// Base 004F1190 for a defense node of the plan (the cell list argument is
// empty). The four quarters around House +5640 (the yard Cell at deploy)
// sum the defense values of the house's buildings in them (00451710,
// 00451760, 004517B0); the quarter with the least total (the first on a
// tie) is defended. Its need for each kind is 0.33 (+15F7C..+15F84) less
// that kind's share of the total. The kind with the largest need (ground
// only when it beats both others; then air only when it beats naval) gives
// the list: 004F1E50 ground, 004F1FF0 air, 004F2190 naval. An empty list
// falls back to air, then naval, then ground. Each list holds the
// BuildingTypes the country owns, with a value of that kind, at the
// house's tech level, whose prerequisites (004EF740) are in the house's
// buildings other than IsBaseDefense types and WallTower (Rules +78C).
// One entry is taken as is; more are weighted by value + 10000 / cost.
// TODO-ORIGINAL: the place search 004F03C0 (a zero Cell fails the node)
// is left to the caller, and upgrades (Building +4C0) do not add values.
std::string PrototypeAI::defense_choice(const Context& context) {
    std::array<std::array<std::int64_t, 3>, 4> quarters{};
    const auto centre = context.own_centre;
    std::vector<std::string> owned;
    const auto tower = rules_.read_string("General", "WallTower");
    for (const auto* building : context.buildings) {
        if (!rules_.read_bool(building->type, "IsBaseDefense") && !equal_name(building->type, tower))
            owned.push_back(building->type);
        if (!centre || !building->cell) continue;
        const int dx = building->cell->x - centre->x, dy = building->cell->y - centre->y;
        if (!dx && !dy) continue;
        // 004BA600 is atan2 on floats; the 16-bit direction is 0 at north
        // and grows clockwise (0079B630 pi/2, 0079B628 -65536/2pi).
        const double angle = std::atan2(static_cast<float>(-dy), static_cast<float>(dx));
        const auto direction = static_cast<std::uint16_t>(static_cast<std::int32_t>((angle - 1.5707963267948966) *
            -10430.060040584269));
        const auto quarter = ((direction >> 13) + 1) >> 1 & 3;
        const auto values = defense_values(building->type);
        for (std::size_t k = 0; k < 3; ++k) quarters[quarter][k] += values[k];
    }
    std::size_t chosen = 0;
    std::int64_t least = 999999;
    for (std::size_t q = 0; q < 4; ++q) {
        const auto total = quarters[q][0] + quarters[q][1] + quarters[q][2];
        if (total < least) { least = total; chosen = q; }
    }
    std::array<float, 3> need{};
    for (std::size_t k = 0; k < 3; ++k)
        need[k] = 0.33f - (least == 0 ? 0.0f : static_cast<float>(quarters[chosen][k]) / static_cast<float>(least));
    std::array<std::vector<std::string>, 3> lists;
    if (const auto* registry = rules_.find_section("BuildingTypes"))
        for (const auto& entry : registry->entries) {
            const auto& type = entry.value;
            if (!contains(rules_.read_list(type, "Owner"), context.computer.country) ||
                rules_.read_integer(type, "TechLevel", -1) > context.computer.tech_level.value_or(10) ||
                !plan_prerequisites(rules_, type, owned, owned.size())) continue;
            const auto values = defense_values(type);
            for (std::size_t k = 0; k < 3; ++k) if (values[k] > 0) lists[k].push_back(type);
        }
    std::size_t kind = need[0] <= need[1] || need[0] <= need[2] ? (need[1] <= need[2] ? 2 : 1) : 0;
    if (lists[kind].empty()) {
        if (!lists[1].empty()) kind = 1;
        else if (!lists[2].empty()) kind = 2;
        else if (!lists[0].empty()) kind = 0;
        else return {};
    }
    const auto& list = lists[kind];
    if (list.size() == 1) return list.front();
    std::vector<std::int64_t> weights;
    std::int64_t sum = 0;
    for (const auto& type : list) {
        const auto cost = rules_.read_integer(type, "Cost");
        weights.push_back(defense_values(type)[kind] + (cost > 0 ? 10000 / cost : 0));
        sum += weights.back();
    }
    if (sum <= 0) return {};
    const auto roll = random(1, static_cast<std::uint32_t>(sum));
    std::int64_t walk = 0;
    for (std::size_t n = 0; n < list.size(); ++n)
        if ((walk += weights[n]) >= roll) return list[n];
    return {};
}

// Base 004E9660 chooses the next building of the base plan. 0042C3D0
// gives the first node that is not built; then:
//  - a Naval node (BuildingType +ADA) is removed while House +138 is
//    clear, and 0042C3D0 is asked once more;
//  - a defense node (-1) is resolved by 004F1190 (defense_choice); when
//    that fails, the node is removed and nothing is chosen this time;
//  - when free power (+52D0 - +15F88) is below the drain (+52D4) plus
//    the node's drain (+C6C), a power node goes in front of the node and
//    nothing is chosen this time. BuildConst types and types without a
//    drain are exempt;
//  - else the node type becomes the building to make (+553C).
// TODO-ORIGINAL: the AIPickWallDefensePercent walls (Rules +BCC, 004F5D30),
// node -3 (004F2350), the upgrade path (+C20) and the replacement skip of
// 0042C3D0 (node +8, 004F64C0) are not modelled. House +15F88 is zero
// (004E18C0) and is taken as zero.
std::string PrototypeAI::plan_build(const Context& context) {
    const auto unbuilt = [&]() {
        for (std::size_t i = 0; i < plan_.size(); ++i) {
            const auto& type = plan_[i];
            if (type.empty()) return i;
            const auto nodes = std::count_if(plan_.begin(), plan_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                [&](const std::string& item) { return equal_name(item, type); });
            if (std::count_if(context.buildings.begin(), context.buildings.end(),
                    [&](const WorldEntity* e) { return equal_name(e->type, type); }) < nodes) return i;
        }
        return plan_.size();
    };
    auto i = unbuilt();
    if (i < plan_.size() && !naval_ && !plan_[i].empty() && rules_.read_bool(plan_[i], "Naval")) {
        plan_.erase(plan_.begin() + static_cast<std::ptrdiff_t>(i));
        i = unbuilt();
    }
    if (i == plan_.size()) return {};
    if (plan_[i].empty()) {
        auto pick = defense_choice(context);
        if (pick.empty()) {
            plan_.erase(plan_.begin() + static_cast<std::ptrdiff_t>(i));
            return {};
        }
        plan_[i] = std::move(pick);
    }
    const auto type = plan_[i];
    const auto drain = std::max<std::int64_t>(0, -static_cast<std::int64_t>(rules_.read_integer(type, "Power")));
    if (drain > 0 && !contains(rules_.read_list("AI", "BuildConst"), type)) {
        std::int64_t output = 0, used = 0;
        for (const auto* building : context.buildings) {
            const auto power = rules_.read_integer(building->type, "Power");
            if (power > 0) output += power;
            else used -= power;
        }
        output = context.computer.power_output.value_or(static_cast<std::int32_t>(output));
        used = context.computer.power_drain.value_or(static_cast<std::int32_t>(used));
        if (output < used + drain) {
            // House +130 side 0 takes GDIPowerPlant (Rules +7AC). Other
            // sides take NodAdvancedPower (+7B8) when 004EF740 accepts it,
            // else NodRegularPower (+7B4). 004E9660 passes the House
            // building list (+6C) to 004EF740, which compares its entries
            // with BuildingType pointers, so only a type without
            // prerequisites can pass.
            std::string power;
            if (context.side.value_or(0) == 0) power = rules_.read_string("General", "GDIPowerPlant");
            else {
                power = rules_.read_string("General", "NodAdvancedPower");
                if (!registered(rules_, "BuildingTypes", power) || !plan_prerequisites(rules_, power, {}, 0))
                    power = rules_.read_string("General", "NodRegularPower");
            }
            if (registered(rules_, "BuildingTypes", power)) {
                plan_.insert(plan_.begin() + static_cast<std::ptrdiff_t>(i), power);
                return {};
            }
        }
    }
    return type;
}

// Base 006BD870 (GetTaskForceMissingMemberTypes): each TaskForce entry
// repeated by its count, less one entry for each member's type.
std::vector<std::string> PrototypeAI::missing_types(const Context& context, const Team& team) const {
    std::vector<std::string> result;
    const auto* force = task_force(team_types_[team.type]);
    if (!force) return result;
    for (const auto& [count, name] : force->members)
        for (std::int32_t i = 0; i < count; ++i) result.push_back(name);
    for (const auto id : team.members) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        const auto at = std::find_if(result.begin(), result.end(),
            [&](const std::string& name) { return equal_name(name, found->second->type); });
        if (at != result.end()) result.erase(at);
    }
    return result;
}

// The AI production choices, one per factory kind, each kept until its
// product leaves the factory (House +5540 vehicles, +5544 infantry,
// +5548 aircraft). The factory building (0044B480) starts the choice when
// it has no product and the house has at least 11 credits. The product
// leaving or being abandoned (004B97C0) clears the choice.
void PrototypeAI::choose_products(Context& context, std::vector<skirmish::Order>& result) {
    static constexpr std::array<const char*, 3> registries{{"VehicleTypes", "InfantryTypes", "AircraftTypes"}};
    static constexpr std::array<WorldKind, 3> kinds{{WorldKind::Unit, WorldKind::Infantry, WorldKind::Aircraft}};
    static constexpr std::array<const char*, 3> factory_kinds{{"UnitType", "InfantryType", "AircraftType"}};
    const auto& world = context.world;
    const auto house = context.computer.id;
    // A product in a factory is a Created object of the house.
    const auto in_factory = [&](std::size_t k) -> const WorldEntity* {
        for (const auto& e : world.entities)
            if (e.house_id == house && e.kind == kinds[k] && e.lifetime == Lifetime::Created) return &e;
        return nullptr;
    };
    for (std::size_t k = 0; k < 3; ++k) {
        auto& choice = choice_[k];
        if (!choice.empty() && choice_product_[k]) {
            const auto product = std::find_if(world.entities.begin(), world.entities.end(),
                [&](const WorldEntity& e) { return e.id == choice_product_[k]; });
            if (product == world.entities.end() || product->lifetime != Lifetime::Created) {
                choice.clear();
                choice_product_[k] = 0;
            }
        }
        // 004E4110 calls each choice function every frame; they return 15,
        // which it ignores.
        if (choice.empty()) {
            choice = k == 0 ? choose_harvester(context) : std::string{};
            if (choice.empty()) choice = choose_product(context, registries[k], kinds[k]);
        }
        if (choice.empty() || choice_product_[k]) continue;
        // 0044B480: the factory starts the choice; a Naval vehicle needs a
        // Naval factory and a land vehicle a land one.
        if (const auto* product = in_factory(k)) {
            if (equal_name(product->type, choice)) choice_product_[k] = product->id;
            continue;
        }
        if (context.cash < 11) continue;
        const auto* plant = factory(rules_, context.buildings, factory_kinds[k], k == 0 && rules_.read_bool(choice, "Naval"));
        if (!plant || std::find(context.busy.begin(), context.busy.end(), plant->id) != context.busy.end()) continue;
        result.push_back(order(skirmish::OrderKind::Build, house, *plant, choice));
        context.busy.push_back(plant->id);
    }
}

// Base 004E9C90 opening: a computer house whose IQ (+194) is at least [IQ]
// Harvester (Rules +10C0) and whose +189 is clear makes the first [General]
// HarvesterUnit (Rules +9EC) its country owns, at its tech level, while it
// has fewer of it than HarvestersPerRefinery (Rules +1414) times the first
// two [AI] BuildRefinery types. There is no money test here.
// TODO-ORIGINAL: skirmish houses have the maximum IQ, and +189 is only
// written by 004E18C0 (clear). The counter that 00491D10 reads is taken as
// owned objects, so a product in a factory counts.
std::string PrototypeAI::choose_harvester(const Context& context) const {
    const auto owned = [&](const std::string& type) {
        return std::count_if(context.world.entities.begin(), context.world.entities.end(), [&](const WorldEntity& e) {
            return e.house_id == context.computer.id && equal_name(e.type, type) &&
                (e.lifetime == Lifetime::Created || (e.lifetime == Lifetime::Placed && e.resolved_hp.value_or(0) > 0));
        });
    };
    std::string harvester;
    for (const auto& type : rules_.read_list("General", "HarvesterUnit"))
        if (registered(rules_, "VehicleTypes", type) && contains(rules_.read_list(type, "Owner"), context.computer.country)) {
            harvester = type;
            break;
        }
    if (harvester.empty()) return {};
    const auto refineries = rules_.read_list("AI", "BuildRefinery");
    std::int64_t refinery_count = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(2, refineries.size()); ++i) refinery_count += owned(refineries[i]);
    if (rules_.read_integer(harvester, "TechLevel", -1) > context.computer.tech_level.value_or(10) ||
        owned(harvester) >= rules_.read_integer("General", "HarvestersPerRefinery") * refinery_count ||
        !buildable(rules_, context.computer, context.actors, context.buildings, harvester, std::nullopt)) return {};
    return harvester;
}

// Base 004E9C90 (VehicleTypes), 004EA1B0 (InfantryTypes) and 004EA4D0
// (AircraftTypes), after the harvester test:
//  - every team of the house that is (Reinforce and not IsFullStrength)
//    or (neither IsForcedActive nor IsHasBeen) adds its missing members
//    (006BD870) of this kind, and keeps the least CreationFrame (+50) of
//    the teams that want each type;
//  - each free object of the type that 004C8D00 calls recruitable takes
//    one request away;
//  - of the wanted types that 004E3660 allows and whose cost is at most
//    Available_Money, the one wanted by the oldest team is taken with
//    FillEarliestTeamProbability (Rules +105C, by AIDifficulty) percent;
//    else a random type of the list kept from the last increase of the
//    greatest request count. Ties keep the lowest registry index.
std::string PrototypeAI::choose_product(const Context& context, const char* registry, WorldKind kind) {
    const auto* section = rules_.find_section(registry);
    if (!section) return {};
    std::vector<std::string> types;
    for (const auto& entry : section->entries) types.push_back(entry.value);
    std::vector<std::int32_t> wanted(types.size(), 0);
    std::vector<std::int64_t> earliest(types.size(), std::numeric_limits<std::int32_t>::max());
    const auto index = [&](const std::string& name) {
        for (std::size_t i = 0; i < types.size(); ++i) if (equal_name(types[i], name)) return i;
        return types.size();
    };
    for (const auto& team : teams_) {
        const auto& type = team_types_[team.type];
        if (!((type.reinforce && !team.full) || (!team.forced_active && !team.has_been))) continue;
        for (const auto& name : missing_types(context, team)) {
            const auto at = index(name);
            if (at == types.size()) continue;
            ++wanted[at];
            earliest[at] = std::min(earliest[at], team.created);
        }
    }
    for (const auto* entity : context.actors) {
        if (entity->kind != kind || !recruitable(context, *entity)) continue;
        const auto at = index(entity->type);
        if (at < types.size() && wanted[at] > 0) --wanted[at];
    }
    std::vector<std::size_t> list;
    std::int32_t most = -1;
    std::size_t best = types.size();
    std::int64_t best_frame = std::numeric_limits<std::int32_t>::max();
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (wanted[i] <= 0 ||
            !buildable(rules_, context.computer, context.actors, context.buildings, types[i], context.cash)) continue;
        if (most == -1 || most < wanted[i]) {
            list.clear();
            most = wanted[i];
        }
        list.push_back(i);
        if (best == types.size() || earliest[i] < best_frame) {
            best_frame = earliest[i];
            best = i;
        }
    }
    const auto roll = static_cast<double>(random(0, 0x7ffffffe)) * 4.656612877414201e-10;
    if (roll < by_difficulty(rules_, "FillEarliestTeamProbability", difficulty_, 0) * 0.01)
        return best < types.size() ? types[best] : std::string{};
    if (list.empty()) return {};
    return types[list[random(0, static_cast<std::uint32_t>(list.size() - 1))]];
}

// Base 004C8D00 (Is_Recruitable for a house): owned by the house, not in
// limbo, in no team, in a mission whose [Mission] Recruitable is set
// (00594DB0) and with Techno +379 set.
// TODO-ORIGINAL: the caller owns missions. A miner is taken to be in
// Harvest (Recruitable=no); other units are in a recruitable mission.
bool PrototypeAI::recruitable(const Context& context, const WorldEntity& entity) const {
    if (entity.house_id != context.computer.id || entity.kind == WorldKind::Structure) return false;
    if (rules_.read_bool(entity.type, "Harvester")) return false;
    for (const auto& team : teams_)
        if (std::find(team.members.begin(), team.members.end(), entity.id) != team.members.end()) return false;
    return true;
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
    if (computer_house_ != computer_house_id) {
        rng_ = Random(0x9e3779b9u ^ computer_house_id);
        // The house timer runs from game start, not from the first AI tick.
        next_team_ = first_team_frame(rules_, difficulty_, computer->original_index.value_or(0));
    }
    computer_house_ = computer_house_id;
    player_house_ = player_house_id;
    last_frame_ = game_frame;
    const bool teams_on = uses_ai_ini();
    const bool build_due = game_frame >= next_build_;
    const bool attack_due = game_frame >= next_attack_;
    // Without AI.INI the old policy keeps its decision clocks. With it,
    // HouseAI (004E4110) and TeamClass::AI (006B7AD0) run every frame.
    if (!teams_on && !build_due && !attack_due) return result;
    if (build_due) next_build_ = static_cast<std::int64_t>(game_frame) + build_interval_frames;
    if (attack_due) next_attack_ = static_cast<std::int64_t>(game_frame) + attack_interval_frames;

    Context context{world, *computer, *player, game_frame, side_index(rules_, *computer), {}, {}, {}, 0, false, {}, {}, {}, {}, {}};
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
    // Base 004E8990 chooses an enemy from base centres; 006BED50 passes
    // no enemy to 0041C990 until that selection succeeds. A deployed base
    // supplies the centre in this adapter. Damage records also select an enemy
    // through 004EEBA0, even if its construction yard is gone.
    // TODO-ORIGINAL: the reference shows no attack in 38 minutes against a
    // player who only deploys the yard. The cause is unknown. 004E8990 and
    // 004EEBA0 can select that player as enemy: they check no structures.
    // Look in 0041C990, team filling or the team scripts.
    context.enemy_available = std::any_of(enemies.begin(), enemies.end(),
        [](const WorldEntity* e) { return e->kind == WorldKind::Structure; });
    if (computer->attacker_damage_records)
        for (const auto& [id, damage] : *computer->attacker_damage_records)
            if (id == player_house_id && damage > 0) context.enemy_available = true;
    if (computer->selected_enemy_index && player->original_index &&
        *computer->selected_enemy_index == *player->original_index && *computer->selected_enemy_index >= 0)
        context.enemy_available = true;
    const auto by_id = [](const WorldEntity* a, const WorldEntity* b) { return a->id < b->id; };
    std::sort(actors.begin(), actors.end(), by_id);
    std::sort(buildings.begin(), buildings.end(), by_id);
    std::sort(enemies.begin(), enemies.end(), by_id);
    if (actors.empty() || enemies.empty()) return result;
    context.cash = static_cast<std::int64_t>(computer->cash.value_or(0));

    // Base 004F5BE0 sets House +53BC to the newest construction yard Cell.
    // It is not cleared when that yard is lost.
    for (const auto house : {computer_house_id, player_house_id}) {
        const WorldEntity* newest = nullptr;
        for (const auto& entity : world.entities)
            if (entity.house_id == house && live(entity) && yard(rules_, entity) && (!newest || entity.id > newest->id))
                newest = &entity;
        if (newest) base_centres_[house] = *newest->cell;
    }
    if (const auto found = base_centres_.find(computer_house_id); found != base_centres_.end())
        context.own_centre = found->second;
    if (const auto found = base_centres_.find(player_house_id); found != base_centres_.end())
        context.enemy_centre = found->second;

    // TODO-ORIGINAL: the computer's MCV deploys on the build clock; the
    // original unit AI deploys it.
    if (build_due && std::none_of(buildings.begin(), buildings.end(), [&](const WorldEntity* e) { return yard(rules_, *e); })) {
        for (const WorldEntity* entity : actors) if (mcv(rules_, *entity)) {
            result.push_back(order(skirmish::OrderKind::Deploy, computer_house_id, *entity));
            break;
        }
    }
    if (teams_on) house_ai(context, result);
    else old_policy(context, build_due, attack_due, result);
    return result;
}

// Base HouseAI 004E4110 for a computer house, in its order: the team
// timer and picker, the enemy choice, then production; and after it the
// Logic pass that runs TeamClass::AI (006B7AD0) for every team.
void PrototypeAI::house_ai(Context& context, std::vector<skirmish::Order>& result) {
    const auto frame = context.frame;
    const auto house = context.computer.id;
    // Base 004F4070 makes the plan (004EF890) and sets +136, +13A and +13B
    // once, when the house first has a base centre.
    if (plan_.empty() && std::any_of(context.buildings.begin(), context.buildings.end(),
            [&](const WorldEntity* e) { return yard(rules_, *e); })) {
        make_plan(context);
        ai_started_ = true;
    }
    // A member that died or left the map leaves its team (006B8FB0).
    for (auto& team : teams_)
        for (const auto id : std::vector<std::uint32_t>(team.members))
            if (!context.own.count(id)) remove_member(context, team, id);
    for (auto it = members_.begin(); it != members_.end();)
        it = context.own.count(it->first) ? std::next(it) : members_.erase(it);
    // The team timer (+5684): when it expires the picker runs, each picked
    // TeamType makes one team (006BEC80, which keeps Max), and the timer
    // restarts with TeamDelays (Rules +E34) by AIDifficulty.
    if (frame >= next_team_) {
        for (const auto& [type, trigger] : pick_teams(context)) {
            const auto max = team_types_[type].max;
            if (max >= 0 && max <= team_count(type)) continue;
            make_team(context, type, trigger);
        }
        next_team_ = static_cast<std::int64_t>(frame) + std::max(1, by_difficulty(rules_, "TeamDelays", difficulty_, 2000));
    }
    // Production mode +12C is 0 in a skirmish: 004E9660 (buildings), then
    // 004E9C90, 004EA1B0 and 004EA4D0 (units), every frame.
    // 004E9660 runs only while +553C is -1. Exit_Object (00440520 case 6)
    // and abandon (004B97C0) clear it, so no new building is chosen while
    // one is in the factory: a Created structure.
    const bool producing = std::any_of(context.world.entities.begin(), context.world.entities.end(), [&](const WorldEntity& e) {
        return e.house_id == house && e.kind == WorldKind::Structure && e.lifetime == Lifetime::Created;
    });
    std::string next;
    if (const auto* construction = factory(rules_, context.buildings, "BuildingType"); construction && !producing) {
        next = plan_build(context);
        if (!next.empty() && registered(rules_, "BuildingTypes", next) &&
            buildable(rules_, context.computer, context.actors, context.buildings, next, std::nullopt, true))
            result.push_back(order(skirmish::OrderKind::Build, house, *construction, next));
    }
    choose_products(context, result);
    // QA trace, only with RA2_AI_PLAN_DEBUG set: one line every 1800 frames.
    if (std::getenv("RA2_AI_PLAN_DEBUG") && frame % 1800 == 0) {
        std::cerr << "AIPLAN frame=" << frame << " house=" << house << " cash=" << context.cash
                  << " producing=" << producing << " next=" << (next.empty() ? "-" : next)
                  << " naval=" << naval_ << " units=" << (choice_[0].empty() ? "-" : choice_[0])
                  << " infantry=" << (choice_[1].empty() ? "-" : choice_[1]) << " teams=" << teams_.size() << " plan=";
        for (const auto& node : plan_) std::cerr << (node.empty() ? "?" : node) << ',';
        std::cerr << " have=";
        for (const auto* b : context.buildings) std::cerr << b->type << ',';
        std::cerr << " making=";
        for (const auto& e : context.world.entities)
            if (e.house_id == house && e.lifetime == Lifetime::Created) std::cerr << e.type << ':' << e.id << ',';
        std::cerr << " teams:";
        for (const auto& t : teams_)
            std::cerr << ' ' << team_types_[t.type].id << '/' << t.members.size() << (t.moving ? 'M' : 'F') << t.line;
        std::cerr << '\n';
    }
    // Logic: TeamClass::AI for each team, oldest first.
    for (std::size_t i = 0; i < teams_.size(); ++i)
        if (!teams_[i].dead) team_ai(context, teams_[i], result);
    teams_.erase(std::remove_if(teams_.begin(), teams_.end(), [](const Team& team) { return team.dead; }), teams_.end());
}

// The old policy without AI.INI: a fixed four-building opening, the
// cheapest tank, and every fighter at the closest enemy.
void PrototypeAI::old_policy(Context& context, bool build_due, bool attack_due, std::vector<skirmish::Order>& result) {
    const auto& computer = context.computer;
    const auto computer_house_id = computer.id;
    const auto& actors = context.actors;
    const auto& buildings = context.buildings;
    auto& cash = context.cash;
    if (build_due) {
        if (const auto* construction = factory(rules_, buildings, "BuildingType")) {
            const auto& side = context.side;
            std::int64_t power = 0;
            for (const WorldEntity* building : buildings) power += rules_.read_integer(building->type, "Power");
            for (const char* role : {"BuildPower", "BuildBarracks", "BuildRefinery", "BuildWeapons"}) {
                const auto types = rules_.read_list("AI", role);
                const bool have = std::any_of(types.begin(), types.end(),
                    [&](const auto& type) { return owns(buildings, type); });
                if (have && !(equal_name(role, "BuildPower") && power < 0)) continue;
                for (const auto& type : types) {
                    const auto* planning_side = rules_.find(type, "AIBasePlanningSide");
                    if (planning_side && rules_.read_integer(type, "AIBasePlanningSide", -1) != -1 &&
                        (!side || rules_.read_integer(type, "AIBasePlanningSide") != *side)) continue;
                    if (!registered(rules_, "BuildingTypes", type) ||
                        !buildable(rules_, computer, actors, buildings, type, cash)) continue;
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
                    !ground_weapon(rules_, type) || !buildable(rules_, computer, actors, buildings, type, cash)) continue;
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
        for (const WorldEntity* enemy : context.enemies) {
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
}

} // namespace ra2::sim
