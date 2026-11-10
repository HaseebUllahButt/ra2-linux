#include "prototype_ai.h"
#include "fs/mix_bootstrap.h"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <map>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
namespace sw = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::PrototypeAI;
using ra2::sim::skirmish::Order;
using ra2::sim::skirmish::OrderKind;
constexpr std::uint32_t computer = 100, player = 200;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_orders(const std::vector<Order>& a, const std::vector<Order>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto key = [](const Order& order) {
            return std::tie(order.kind, order.house, order.entity, order.target, order.type, order.cell.x, order.cell.y);
        };
        if (key(a[i]) != key(b[i])) return false;
    }
    return true;
}

sw::House house(std::uint32_t id, const std::string& country, bool human) {
    sw::House result;
    result.id = id;
    result.country = country;
    result.human_controlled = human;
    result.cash = 10000;
    result.tech_level = 10;
    return result;
}

sw::Entity actor(const IniFile& rules, std::uint32_t id, std::uint32_t owner,
                 std::string type, sw::Kind kind, ra2map::Cell cell) {
    sw::Entity result;
    result.id = id;
    result.house_id = owner;
    result.type = std::move(type);
    result.kind = kind;
    result.cell = cell;
    result.position = ra2::sim::Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
    result.resolved_hp = rules.read_integer(result.type, "Strength", 100);
    result.maximum_hp = result.resolved_hp;
    result.lifetime = sw::Lifetime::Placed;
    result.active = true;
    result.in_limbo = false;
    result.on_map = true;
    result.target_id = 0;
    return result;
}

// A unit moved by the test: its Cell and lepton position agree.
void move_to(sw::Entity& entity, ra2map::Cell cell) {
    entity.cell = cell;
    entity.position = ra2::sim::Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
}

struct Faction { const char* country; const char* mcv; const char* yard; const char* tank;
                 std::array<const char*, 4> buildings; };

sw::Snapshot start(const IniFile& rules, const Faction& faction) {
    sw::Snapshot world;
    world.houses = {house(computer, faction.country, false), house(player, "Americans", true), house(300, "Neutral", false)};
    world.entities = {
        actor(rules, 21, computer, faction.tank, sw::Kind::Unit, {12, 10}),
        actor(rules, 4, player, "MTNK", sw::Kind::Unit, {30, 30}),
        actor(rules, 20, computer, faction.mcv, sw::Kind::Unit, {10, 10}),
        actor(rules, 3, player, "AMCV", sw::Kind::Unit, {32, 30}),
        actor(rules, 7, 300, "MTNK", sw::Kind::Unit, {12, 11}),
        actor(rules, 1, player, "MTNK", sw::Kind::Unit, {12, 10}),
        actor(rules, 2, player, "MTNK", sw::Kind::Aircraft, {12, 10})
    };
    world.entities[5].lifetime = sw::Lifetime::Destroyed;
    world.entities[5].resolved_hp = 0;
    world.random.first = 13;
    world.random.table[4] = 0x12345678;
    return world;
}

void check_base_match(const IniFile& rules, const Faction& faction) {
    auto world = start(rules, faction);
    auto shuffled = world;
    std::reverse(shuffled.entities.begin(), shuffled.entities.end());
    std::reverse(shuffled.houses.begin(), shuffled.houses.end());
    PrototypeAI ai(rules), replay(rules);
    const auto first = ai.tick(world, computer, player, 0);
    require(first.size() == 2 && first[0].kind == OrderKind::Deploy && first[0].entity == 20 &&
        first[1].kind == OrderKind::Attack && first[1].entity == 21 && first[1].target == 4 &&
        first[1].house == computer && first[1].cell.x == 30 && first[1].cell.y == 30,
        "Starting computer did not deploy and attack the closest live player ground unit");
    require(same_orders(first, replay.tick(shuffled, computer, player, 0)), "Storage order changed computer decisions");
    require(world.entities[2].type == faction.mcv && world.entities[0].target_id == 0 &&
        world.entities[0].cell->x == 12 && world.houses[0].cash == 10000 &&
        world.random.first == 13 && world.random.table[4] == 0x12345678,
        "Computer decisions changed the shared world or random state");
    require(ai.tick(world, computer, player, 0).empty() && ai.tick(world, computer, player, 44).empty(),
        "Computer repeated decisions before their interval");
    const auto attack = ai.tick(world, computer, player, 45);
    require(attack.size() == 1 && attack[0].kind == OrderKind::Attack &&
        ai.tick(world, computer, player, 89).empty(), "Attack interval changed");

    // This fixture advances only observed completed products. Execution belongs
    // to the production, movement and combat runtimes, not the AI test driver.
    world.entities[2] = actor(rules, 20, computer, faction.yard, sw::Kind::Structure, {10, 10});
    std::int32_t frame = 90;
    std::uint32_t id = 22;
    for (const auto* building : faction.buildings) {
        const auto orders = ai.tick(world, computer, player, frame);
        require(orders.size() == 2 && orders[0].kind == OrderKind::Build && orders[0].type == building &&
            orders[0].house == computer && orders[0].entity == 20 && orders[0].cell.x == 10 &&
            orders[0].cell.y == 10 && orders[1].kind == OrderKind::Attack,
            "Computer did not advance its faction base from installed base rules");
        *world.houses[0].cash -= rules.read_integer(building, "Cost");
        world.entities.push_back(actor(rules, id, computer, building, sw::Kind::Structure,
            {static_cast<std::uint16_t>(10 + id * 2), 10}));
        ++id;
        frame += PrototypeAI::build_interval_frames;
    }
    const auto tank = ai.tick(world, computer, player, frame);
    if (tank.empty() || tank[0].type != faction.tank)
        throw std::runtime_error(std::string(faction.country) + " tank policy expected " + faction.tank +
            ", requested " + (tank.empty() ? "no order" : tank[0].type));
    require(tank.size() == 2 && tank[0].kind == OrderKind::Build && tank[0].type == faction.tank &&
        tank[0].entity == 25 && tank[1].kind == OrderKind::Attack,
        "Computer did not request its buildable faction tank from the war factory");

    // Existing forces keep fighting while cash is short. Dead targets disappear.
    world.houses[0].cash = rules.read_integer(faction.tank, "Cost") - 1;
    world.entities[1].resolved_hp = 0;
    const auto retarget = ai.tick(world, computer, player, frame + 90);
    require(retarget.size() == 1 && retarget[0].kind == OrderKind::Attack && retarget[0].target == 3,
        "Computer spent unavailable cash or kept attacking a dead target");
    world.entities[3].lifetime = sw::Lifetime::Destroyed;
    require(ai.tick(world, computer, player, frame + 180).empty(), "Computer kept issuing orders after ground targets disappeared");
}

void check_rule_filters() {
    std::ostringstream text;
    text << "[General]\nPrerequisiteFactory=WORKS\n[VehicleTypes]\n";
    const std::array<const char*, 10> types{{"BANNED", "SHIP", "MINER", "ANTIAIR", "FUTURE", "LOCKED", "OTHER", "LIMITED", "PARASITE", "NATIVE_TANK"}};
    for (std::size_t i = 0; i < types.size(); ++i) text << i << '=' << types[i] << '\n';
    text << "[WORKS]\nFactory=UnitType\nStrength=100\n[SHOT]\nDamage=20\nProjectile=GROUND\n"
            "[AA_SHOT]\nDamage=20\nProjectile=AIR\n[PARASITE_SHOT]\nDamage=20\nProjectile=GROUND\nLimboLaunch=yes\n"
            "[GROUND]\nAG=yes\n[AIR]\nAG=no\n";
    for (const auto* type : types) {
        const std::string name = type;
        text << '[' << name << "]\nCategory=AFV\nSpeed=5\nStrength=100\nPrerequisite="
             << (name == "LOCKED" ? "LAB" : "FACTORY") << "\nTechLevel=" << (name == "FUTURE" ? 11 : 1)
             << "\nOwner=" << (name == "OTHER" ? "OtherCountry" : "LocalCountry") << "\nCost="
             << (name == "NATIVE_TANK" ? 100 : 1) << "\nPrimary="
             << (name == "ANTIAIR" ? "AA_SHOT" : name == "PARASITE" ? "PARASITE_SHOT" : "SHOT") << '\n';
        if (name == "BANNED") text << "ForbiddenHouses=LocalCountry\n";
        if (name == "SHIP") text << "Naval=yes\n";
        if (name == "MINER") text << "Harvester=yes\n";
        if (name == "LIMITED") text << "BuildLimit=1\n";
    }
    IniFile rules;
    rules.parse(text.str());
    sw::Snapshot world;
    world.houses = {house(computer, "LocalCountry", false), house(player, "OtherCountry", true)};
    world.houses[0].cash = 100;
    world.entities = {actor(rules, 10, computer, "WORKS", sw::Kind::Structure, {1, 1}),
        actor(rules, 11, computer, "LIMITED", sw::Kind::Unit, {2, 1}),
        actor(rules, 20, player, "NATIVE_TANK", sw::Kind::Unit, {3, 1})};
    PrototypeAI ai(rules);
    const auto orders = ai.tick(world, computer, player, 0);
    require(orders.size() == 2 && orders[0].kind == OrderKind::Build && orders[0].type == "NATIVE_TANK" &&
        orders[0].entity == 10, "Rule filters selected a forbidden, locked, naval, limited, or noncombat product");
    world.houses[0].cash = 99;
    const auto poor = ai.tick(world, computer, player, 90);
    require(poor.size() == 1 && poor[0].kind == OrderKind::Attack, "Build policy ignored the authored cost");
    world.houses[0].cash = 100;
    world.houses[0].tech_level = 0;
    const auto low_tech = ai.tick(world, computer, player, 180);
    require(low_tech.size() == 1 && low_tech[0].kind == OrderKind::Attack, "Build policy ignored the house tech level");
    world.houses[0].tech_level = 10;
    world.entities[0].lifetime = sw::Lifetime::Destroyed;
    const auto no_factory = ai.tick(world, computer, player, 270);
    require(no_factory.size() == 1 && no_factory[0].kind == OrderKind::Attack, "Computer built from a destroyed factory");
}

void check_clock_and_guards(const IniFile& rules, const Faction& faction) {
    auto world = start(rules, faction);
    PrototypeAI ai(rules), clean(rules);
    const auto expected = clean.tick(world, computer, player, 0);
    require(ai.tick(world, computer, player, -1).empty() && ai.tick(world, computer, computer, 0).empty() &&
        ai.tick(world, 999, player, 0).empty(), "Computer accepted invalid frame or house IDs");
    world.pending_cell_setup = true;
    require(ai.tick(world, computer, player, 0).empty(), "Computer used a world with pending Cell setup");
    world.pending_cell_setup = false;
    world.houses[0].human_controlled = true;
    require(ai.tick(world, computer, player, 0).empty(), "Computer controlled a human house");
    world.houses[0].human_controlled = false;
    require(same_orders(expected, ai.tick(world, computer, player, 123)), "First decision required frame zero");
    require(same_orders(expected, ai.tick(world, computer, player, 0)), "Frame rewind did not reset decisions");
    const auto last = std::numeric_limits<std::int32_t>::max();
    require(same_orders(expected, ai.tick(world, computer, player, last)) &&
        ai.tick(world, computer, player, last).empty(), "Decision clocks overflowed or repeated a frame");
    ai.reset();
    require(same_orders(expected, ai.tick(world, computer, player, last)), "Explicit reset retained decision clocks");
}

// ---- AI.INI teams ---------------------------------------------------------
std::vector<std::string> fields(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto comma = value.find(',', start);
        result.emplace_back(IniFile::trim(std::string_view(value).substr(start, comma - start)));
        if (comma == std::string::npos) return result;
        start = comma + 1;
    }
}

std::vector<std::pair<int, std::string>> task_force(const IniFile& ai, const std::string& team_type) {
    std::vector<std::pair<int, std::string>> result;
    const auto id = ai.read_string(team_type, "TaskForce");
    for (int i = 0; i < 6; ++i) {
        const auto parts = fields(ai.read_string(id, std::to_string(i)));
        if (parts.size() >= 2) result.emplace_back(std::stoi(parts[0]), parts[1]);
    }
    return result;
}

bool listed(const IniFile& rules, const char* registry, const std::string& type) {
    const auto* section = rules.find_section(registry);
    return section && std::any_of(section->entries.begin(), section->entries.end(),
        [&](const auto& entry) { return entry.value == type; });
}

sw::Kind kind_of(const IniFile& rules, const std::string& type) {
    return listed(rules, "InfantryTypes", type) ? sw::Kind::Infantry :
        listed(rules, "AircraftTypes", type) ? sw::Kind::Aircraft : sw::Kind::Unit;
}

// Base 004E2E40: the first team pick waits for the house team timer.
// Test houses have no array index, so it is zero.
std::int32_t at(const IniFile& rules, const PrototypeAI& policy) {
    return PrototypeAI::first_team_frame(rules, policy.difficulty(), 0);
}

IniFile parse(const std::string& text) {
    IniFile result;
    result.parse(text);
    return result;
}

// A computer house starts its AI (+13A, set by the MCV deploy 006FDFC0)
// only with a construction yard; these synthetic matches add one.
std::string yard_rules(const std::string& text) { return text + "[YARD]\nConstructionYard=yes\n"; }
void add_yard(const IniFile& rules, sw::Snapshot& world) {
    world.entities.push_back(actor(rules, 9, computer, "YARD", sw::Kind::Structure, {2, 2}));
}

// Every frame from `from` to `to`, inclusive; all orders in frame order.
std::vector<Order> frames(PrototypeAI& policy, const sw::Snapshot& world, std::int32_t from, std::int32_t to) {
    std::vector<Order> result;
    for (auto frame = from; frame <= to; ++frame) {
        auto orders = policy.tick(world, computer, player, frame);
        result.insert(result.end(), orders.begin(), orders.end());
    }
    return result;
}

void check_base_teams(const IniFile& installed_rules, const IniFile& ai, const Faction& faction, const char* enemy_country,
                      const Faction& enemy, PrototypeAI::Difficulty level, const char* side) {
    // Exercise all authored offensive triggers independently of the defense
    // opening, which check_original_trigger_gates tests with its real gate.
    std::ostringstream text;
    for (const auto& section : installed_rules.sections()) {
        text << '[' << section.name << "]\n";
        for (const auto& entry : section.entries)
            text << entry.key << '=' << (entry.key == "UseMinDefenseRule" ? "no" : entry.value) << '\n';
    }
    IniFile rules;
    rules.parse(text.str());
    sw::Snapshot world;
    world.houses = {house(computer, faction.country, false), house(player, enemy_country, true)};
    world.entities.push_back(actor(rules, 10, computer, faction.yard, sw::Kind::Structure, {10, 10}));
    std::uint32_t id = 11;
    for (const auto* type : faction.buildings)
        world.entities.push_back(actor(rules, id++, computer, type, sw::Kind::Structure, {static_cast<std::uint16_t>(id * 3), 10}));
    world.entities.push_back(actor(rules, 30, player, enemy.yard, sw::Kind::Structure, {60, 60}));
    world.entities.push_back(actor(rules, 31, player, enemy.buildings[0], sw::Kind::Structure, {64, 60}));
    world.entities.push_back(actor(rules, 32, player, enemy.tank, sw::Kind::Unit, {58, 58}));
    const auto before = world;
    PrototypeAI policy(rules, &ai, level), replay(rules, &ai, level);
    require(policy.uses_ai_ini() && policy.difficulty() == level, "Base AI.INI was not loaded");
    require(policy.trigger_count() > 80 && policy.trigger_weight("0CA6621C-G") == 40.0,
        "Base AITriggerTypes were not parsed with their authored weights");
    const auto first = policy.tick(world, computer, player, at(rules, policy) + 0);
    require(same_orders(first, replay.tick(world, computer, player, at(rules, replay) + 0)), "Team choice was not deterministic");
    require(world.houses[0].cash == before.houses[0].cash && world.entities.size() == before.entities.size() &&
        world.random.first == before.random.first, "Team policy changed the shared world");
    const auto teams = policy.teams();
    require(!teams.empty(), "Computer did not pick a base AI.INI team");
    const std::size_t enabled_field = level == PrototypeAI::Difficulty::Easy ? 15 :
        level == PrototypeAI::Difficulty::Normal ? 16 : 17;
    std::vector<std::string> wanted;
    for (const auto& team : teams) {
        const auto f = fields(ai.read_string("AITriggerTypes", team.trigger));
        require(f.size() >= 18 && f[10] == "1" && f[enabled_field] == "1" && (f[12] == "0" || f[12] == side) &&
            (f[2] == "<all>" || f[2] == faction.country) && std::stoi(f[3]) <= 10 &&
            (f[1] == team.team_type || f[14] == team.team_type),
            "Picked AI trigger is not enabled for this skirmish house, side and difficulty");
        require(team.forming && team.members.empty(), "New team did not start empty and forming");
        for (const auto& [count, type] : task_force(ai, team.team_type)) { (void)count; wanted.push_back(type); }
    }
    std::vector<std::uint32_t> factories;
    for (const auto& order : first) {
        if (order.kind != OrderKind::Build || listed(rules, "BuildingTypes", order.type)) continue;
        // Base 004E9C90: the refinery has no harvester, so the war factory
        // makes one before any team member.
        require(std::find(wanted.begin(), wanted.end(), order.type) != wanted.end() ||
            (rules.read_bool(order.type, "Harvester") && factories.empty() &&
             rules.read_string(world.entities[4].type, "Factory") == "UnitType" && order.entity == world.entities[4].id),
            "Computer built a unit no forming team asked for");
        const auto owners = IniFile::split_list(rules.read_string(order.type, "Owner"));
        require(std::find(owners.begin(), owners.end(), faction.country) != owners.end(),
            "Computer built a unit its country may not own");
        const auto& plant = *std::find_if(world.entities.begin(), world.entities.end(),
            [&](const sw::Entity& e) { return e.id == order.entity; });
        const auto factory = rules.read_string(plant.type, "Factory");
        require(plant.house_id == computer && factory == (listed(rules, "InfantryTypes", order.type) ? "InfantryType" :
            listed(rules, "VehicleTypes", order.type) ? "UnitType" : "AircraftType"),
            "Team member was requested from the wrong factory");
        require(std::find(factories.begin(), factories.end(), order.entity) == factories.end(),
            "One factory got two production requests in one decision");
        factories.push_back(order.entity);
    }

    // Completed products appear as free units. Each frame recruits one
    // object per TaskForce entry (006B91C0); full teams launch and run
    // their scripts.
    id = 100;
    for (const auto& team : teams)
        for (const auto& [count, type] : task_force(ai, team.team_type))
            for (int i = 0; i < count; ++i)
                world.entities.push_back(actor(rules, id++, computer, type, kind_of(rules, type), {14, 14}));
    const auto launch = frames(policy, world, at(rules, policy) + 1, at(rules, policy) + 12);
    std::map<std::string, double> weights;
    std::map<std::string, int> launched;
    bool ordered = false;
    for (const auto& team : policy.teams()) {
        weights[team.trigger] = *policy.trigger_weight(team.trigger);
        if (team.forming) continue;
        require(team.script_line >= 0, "Launched team did not start its script");
        ++launched[team.trigger];
        ordered = ordered || std::all_of(team.members.begin(), team.members.end(), [&](std::uint32_t member) {
            return std::any_of(launch.begin(), launch.end(), [&](const Order& o) { return o.entity == member; });
        });
    }
    require(ordered, "No launched team gave its members script orders");
    require(!launched.empty(), "Full base team did not launch");
    for (auto& entity : world.entities) if (entity.id >= 100) entity.lifetime = sw::Lifetime::Destroyed;
    policy.tick(world, computer, player, at(rules, policy) + 13);
    for (const auto& [trigger, count] : launched) {
        const auto f = fields(ai.read_string("AITriggerTypes", trigger));
        double expected = weights[trigger];
        for (int i = 0; i < count; ++i) expected = std::max(std::stod(f[8]), expected - 50.0);
        require(std::abs(*policy.trigger_weight(trigger) - expected) < 1e-9,
            "Destroyed team did not lower its trigger weight by the rules failure delta");
    }
}


// Base 006BDDB0 (action 54) and 006BDAA0 (action 53) on the authored base
// AI.INI ground teams that open with 54,0 then 53,0 and use the enemy
// credits condition. Each team gathers AISafeDistance cells outside its own
// base toward the enemy, then AISafeDistance cells outside the enemy base,
// then runs line 2.
void check_authored_gather_teams(const IniFile& installed_rules, const IniFile& ai,
                                 const Faction& allied, const Faction& soviet) {
    std::ostringstream text;
    for (const auto& section : installed_rules.sections()) {
        text << '[' << section.name << "]\n";
        for (const auto& entry : section.entries)
            text << entry.key << '=' << (entry.key == "UseMinDefenseRule" ? "no" : entry.value) << '\n';
    }
    IniFile rules;
    rules.parse(text.str());
    require(rules.read_integer("General", "AISafeDistance") == 20, "Base AISafeDistance changed");
    const auto copy = [&](std::ostringstream& out, const std::string& name) {
        out << '[' << name << "]\n";
        if (const auto* section = ai.find_section(name))
            for (const auto& entry : section->entries) out << entry.key << '=' << entry.value << '\n';
    };
    std::size_t checked = 0, gated = 0;
    for (const auto& entry : ai.find_section("AITriggerTypes")->entries) {
        const auto f = fields(entry.value);
        if (f.size() < 18 || f[10] != "1" || f[4] != "4") continue;
        const auto script = ai.read_string(f[1], "Script");
        if (ai.read_string(script, "0") != "54,0" || ai.read_string(script, "1") != "53,0") continue;
        const auto members = task_force(ai, f[1]);
        if (members.empty() || std::any_of(members.begin(), members.end(), [&](const auto& m) {
                return !listed(rules, "InfantryTypes", m.second) && !(listed(rules, "VehicleTypes", m.second) &&
                    !rules.read_bool(m.second, "Naval"));
            })) continue;
        const auto& own = f[12] == "2" ? soviet : allied;
        const auto& enemy = f[12] == "2" ? allied : soviet;
        const auto level = f[17] == "1" ? PrototypeAI::Difficulty::Hard :
            f[16] == "1" ? PrototypeAI::Difficulty::Normal : PrototypeAI::Difficulty::Easy;
        std::ostringstream one;
        one << "[TaskForces]\n0=" << ai.read_string(f[1], "TaskForce") << "\n[ScriptTypes]\n0=" << script
            << "\n[TeamTypes]\n0=" << f[1] << "\n[AITriggerTypes]\n" << entry.key << '=' << entry.value << '\n';
        copy(one, ai.read_string(f[1], "TaskForce"));
        copy(one, script);
        copy(one, f[1]);
        const auto single = parse(one.str());
        for (const bool enemy_yard : {true, false}) {
            sw::Snapshot world;
            world.houses = {house(computer, f[2] == "<all>" ? own.country : f[2], false),
                            house(player, enemy.country, true)};
            world.entities.push_back(actor(rules, 10, computer, own.yard, sw::Kind::Structure, {20, 20}));
            std::uint32_t id = 11;
            for (const auto* type : own.buildings)
                world.entities.push_back(actor(rules, id, computer, type, sw::Kind::Structure,
                    {static_cast<std::uint16_t>(id), 30}));
            if (enemy_yard) world.entities.push_back(actor(rules, 30, player, enemy.yard, sw::Kind::Structure, {80, 20}));
            world.entities.push_back(actor(rules, 31, player, enemy.buildings[0], sw::Kind::Structure, {84, 24}));
            world.entities.push_back(actor(rules, 32, player, enemy.tank, sw::Kind::Unit, {86, 24}));
            id = 100;
            for (const auto& [count, type] : members)
                for (int i = 0; i < count; ++i)
                    world.entities.push_back(actor(rules, id++, computer, type, kind_of(rules, type), {22, 22}));
            PrototypeAI policy(rules, &single, level);
            // One object per entry and frame joins; the full team regroups
            // (IsReforming) and then starts line 0.
            auto frame = at(rules, policy);
            std::vector<Order> last;
            for (; frame < at(rules, policy) + 30; ++frame) {
                last = policy.tick(world, computer, player, frame);
                if (policy.teams().size() == 1 && !policy.teams()[0].forming && policy.teams()[0].script_line == 0) break;
            }
            require(policy.teams().size() == 1 && !policy.teams()[0].forming && policy.teams()[0].script_line == 0,
                "Authored 54/53 ground team did not launch from free units");
            const auto team = policy.teams()[0].members;
            // Team orders only; the base plan may also order a building.
            const auto of_team = [&](std::vector<Order> list) {
                list.erase(std::remove_if(list.begin(), list.end(), [&](const Order& o) {
                    return std::find(team.begin(), team.end(), o.entity) == team.end();
                }), list.end());
                return list;
            };
            const auto moved_to = [&](const std::vector<Order>& list, ra2map::Cell cell) {
                return list.size() == team.size() && std::all_of(list.begin(), list.end(), [&](const Order& o) {
                    return o.kind == OrderKind::Move && o.cell.x == cell.x && o.cell.y == cell.y;
                });
            };
            const auto place = [&](ra2map::Cell cell) {
                for (auto& e : world.entities)
                    if (std::find(team.begin(), team.end(), e.id) != team.end()) move_to(e, cell);
            };
            // Ticks until the script reaches `line`; returns the team orders.
            const auto until_line = [&](std::int32_t line) {
                std::vector<Order> list;
                for (int i = 0; i < 6 && policy.teams().size() == 1 && policy.teams()[0].script_line < line; ++i)
                    list = of_team(policy.tick(world, computer, player, ++frame));
                return list;
            };
            auto orders = of_team(last);
            require(moved_to(orders, {40, 20}) == enemy_yard,
                "Action 54 did not gather AISafeDistance cells from the own yard toward the enemy yard");
            if (!enemy_yard) {
                // The enemy is selected, but it has no base centre: 54 aims
                // toward Coord zero and 53 completes at once (006BDAA0).
                require(orders.size() == team.size() && orders[0].kind == OrderKind::Move,
                    "Action 54 did not move when the enemy had no base centre");
                place(orders[0].cell);
                orders = until_line(2);
                require(policy.teams()[0].script_line >= 2 &&
                    std::none_of(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::Move; }),
                    "Action 53 did not complete without an enemy base centre");
                ++gated;
                continue;
            }
            orders = of_team(policy.tick(world, computer, player, ++frame));
            require(orders.empty() && policy.teams()[0].script_line == 0, "Action 54 finished before arrival");
            place({42, 21}); // Within RelaxedStray (3 cells) of the gather point.
            orders = until_line(1);
            require(policy.teams()[0].script_line == 1 && moved_to(orders, {60, 20}),
                "Action 53 did not gather AISafeDistance cells from the enemy yard toward the own yard");
            place({60, 20});
            // Lines without a target end at once; one line runs per frame.
            orders = until_line(2);
            for (int i = 0; i < 6 && orders.empty(); ++i) orders = of_team(policy.tick(world, computer, player, ++frame));
            require(policy.teams()[0].script_line >= 2 && !orders.empty() &&
                std::all_of(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::Attack; }),
                "Team did not attack after gathering outside the enemy base");
            ++checked;
        }
    }
    // Base AI.INI has 19 such credit-condition teams; two are naval.
    require(checked == 17 && gated == 17, "Authored 54/53 ground team count changed");
    std::cout << "Authored 54/53 ground teams checked: " << checked << '\n';
}

void check_team_leader() {
    // Base 006BB480 asks the member with the highest LeadershipRating
    // (+544) for the target. The members first meet at the Zone (the mean
    // member Cell, 006B9610), then attack (006B9BB0).
    const auto rules = parse(yard_rules(
        "[General]\nTeamDelays=100,100,100\n"
        "[InfantryTypes]\n0=GI\n1=OFFICER\n"
        "[GI]\nLeadershipRating=1\n[OFFICER]\nLeadershipRating=9\n[BARR]\nFactory=InfantryType\n"));
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n1=1,OFFICER\n"
        "[ScriptTypes]\n0=S\n[S]\n0=0,2\n[TeamTypes]\n0=T\n"
        "[T]\nTaskForce=TF\nScript=S\nMax=1\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {
        actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}),
        actor(rules, 11, computer, "OFFICER", sw::Kind::Infantry, {80,80}),
        actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {41,41}),
        actor(rules, 21, player, "BASE", sw::Kind::Structure, {50,50})};
    add_yard(rules, world);
    PrototypeAI policy(rules, &ai);
    const auto start = at(rules, policy);
    // Frame 1 picks the team and recruits one object per TaskForce entry.
    policy.tick(world, computer, player, start);
    require(policy.teams().size() == 1 && policy.teams()[0].members.size() == 2 && policy.teams()[0].forming,
        "Team did not recruit one object per TaskForce entry");
    // Frame 2 launches it. Filling changed IsUnderStrength, so 006B8BB0
    // set IsReforming: the members first regroup at the Zone, the mean
    // member Cell (006B9610, 006B9F40).
    auto orders = policy.tick(world, computer, player, start + 1);
    require(!policy.teams()[0].forming && policy.teams()[0].script_line == -1 && orders.size() == 2 &&
        std::all_of(orders.begin(), orders.end(), [](const Order& o) {
            return o.kind == OrderKind::Move && o.cell.x == 45 && o.cell.y == 45;
        }), "Filled team did not regroup at the Zone");
    // Within Stray of the Zone; base 20 is nearer the GI, 21 the officer.
    for (auto& e : world.entities) if (e.id == 10) move_to(e, ra2map::Cell{44, 44});
    for (auto& e : world.entities) if (e.id == 11) move_to(e, ra2map::Cell{46, 46});
    policy.tick(world, computer, player, start + 2); // Regrouped: the members guard.
    orders = policy.tick(world, computer, player, start + 3);
    require(policy.teams()[0].script_line == 0 && policy.teams()[0].target == 21,
        "Team target search did not use the highest LeadershipRating");
    require(orders.size() == 2 && orders[0].kind == OrderKind::Attack && orders[0].target == 21 &&
        orders[1].target == 21, "Members did not attack the leader's target");
}

void check_original_trigger_gates() {
    const auto rules = parse(yard_rules(
        "[General]\nTeamDelays=90,90,90\nUseMinDefenseRule=yes\n"
        "MinimumAIDefensiveTeams=1,1,1\nMaximumAIDefensiveTeams=0,0,0\n"
        "[InfantryTypes]\n0=GI\n[VehicleTypes]\n0=TANK\n[BARR]\nFactory=InfantryType\n"));
    const std::string records =
        "[TaskForces]\n0=TF\n1=TF2\n[TF]\n0=1,GI\n[TF2]\n0=1,TANK\n"
        "[ScriptTypes]\n0=S\n[S]\n0=0,2\n[TeamTypes]\n0=T\n1=T2\n";
    const auto trigger = [](const char* second) {
        return std::string("[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,") +
            second + ",1,1,1\n";
    };
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}),
        actor(rules, 11, computer, "GI", sw::Kind::Infantry, {10,11}),
        actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    add_yard(rules, world);
    // 0041C990: the minimum defense rule rejects offense before weighted choice.
    const auto offense = parse(records + "[T]\nTaskForce=TF\nScript=S\nMax=5\n" + trigger("<none>"));
    PrototypeAI offensive(rules, &offense);
    offensive.tick(world, computer, player, at(rules, offensive) + 0);
    require(offensive.teams().empty(), "Attack team bypassed the minimum defense rule");
    const auto no_min_rules = parse(yard_rules("[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"));
    auto idle_world = world;
    idle_world.entities.erase(idle_world.entities.begin() + 3); // the player base
    PrototypeAI idle(no_min_rules, &offense);
    idle.tick(idle_world, computer, player, at(no_min_rules, idle) + 0);
    require(idle.teams().empty(), "AI formed an attack team before the player deployed a base");
    idle_world.entities.push_back(world.entities[3]);
    idle.tick(idle_world, computer, player, at(no_min_rules, idle) + 2000);
    require(idle.teams().size() == 1, "Player base did not enable enemy-dependent team creation");
    // 0041C990: both task forces must be available, even if the first is defense.
    const auto paired = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=5\nIsBaseDefense=yes\n"
        "[T2]\nTaskForce=TF2\nScript=S\nMax=5\nIsBaseDefense=yes\n" + trigger("T2"));
    PrototypeAI pair(rules, &paired);
    pair.tick(world, computer, player, at(rules, pair) + 0);
    require(pair.teams().empty(), "Trigger created only the available half of a team pair");
    // Defense comes from TeamType, rather than the trigger's field 13.
    const auto defense = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=5\nIsBaseDefense=yes\n" + trigger("<none>"));
    PrototypeAI defensive(rules, &defense);
    defensive.tick(world, computer, player, at(rules, defensive) + 0);
    defensive.tick(world, computer, player, at(rules, defensive) + 90);
    require(defensive.teams().size() == 1, "Defense limit used the trigger flag instead of the team flag");
    // A negative Max is unlimited in 0041C990.
    const auto unlimited = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=-1\nIsBaseDefense=yes\n" + trigger("<none>"));
    PrototypeAI no_limit(rules, &unlimited);
    no_limit.tick(world, computer, player, at(rules, no_limit) + 0);
    require(no_limit.teams().size() == 1, "Negative team Max prevented all creation");
}

void check_first_team_timer() {
    // Base 004E2E40 arms the team timer at TeamDelays[difficulty] plus
    // 175 frames per house index. 004E4110 calls 006BED50 only after that.
    const auto rules = parse(yard_rules("[General]\nTeamDelays=2000,2500,3500\n[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"));
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[ScriptTypes]\n0=S\n[S]\n0=0,2\n[TeamTypes]\n0=T\n"
        "[T]\nTaskForce=TF\nScript=S\nMax=5\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    require(PrototypeAI::first_team_frame(rules, PrototypeAI::Difficulty::Easy, 1) == 3675 &&
        PrototypeAI::first_team_frame(rules, PrototypeAI::Difficulty::Normal, 0) == 2500 &&
        PrototypeAI::first_team_frame(rules, PrototypeAI::Difficulty::Hard, 2) == 2350,
        "First team frame did not use TeamDelays and the house index");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.houses[0].original_index = 1;
    world.houses[1].original_index = 0;
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}), actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    add_yard(rules, world);
    PrototypeAI easy(rules, &ai);
    for (const auto frame : {0, 90, 3500, 3674}) {
        easy.tick(world, computer, player, frame);
        require(easy.teams().empty(), "Easy computer picked a team before its first team timer");
    }
    easy.tick(world, computer, player, 3675);
    require(easy.teams().size() == 1, "Easy computer did not pick a team when its timer expired");
    easy.tick(world, computer, player, 3675 + 3499);
    require(easy.teams().size() == 1, "Second pick did not wait for TeamDelays");
    easy.tick(world, computer, player, 3675 + 3500);
    require(easy.teams().size() == 2, "Second pick did not happen after TeamDelays");
}

void check_base_plan() {
    // Base 004EF890 plan: yard, BuildPower, then passes over AIBuildThis
    // types (barracks and war factory first) whose prerequisites are in
    // earlier passes; then 2 - AIDifficulty more refineries and
    // [Allied|Soviet]BaseDefenseCounts defense nodes. 0042C3D0 builds the
    // first node that is not built.
    const auto rules = parse(
        "[General]\nAlliedBaseDefenseCounts=2,1,0\nSovietBaseDefenseCounts=5,5,5\n"
        "PrerequisitePower=POWER\nPrerequisiteBarracks=BARR\nPrerequisiteProc=REF\n"
        "[Sides]\nAllies=Local\nSoviets=Enemy\n"
        "[BuildingTypes]\n0=YARD\n1=POWER\n2=BARR\n3=REF\n4=WORK\n5=RADAR\n6=SKIP\n7=DEF\n"
        "[AI]\nBuildConst=YARD\nBuildPower=POWER\nBuildRefinery=REF\nBuildBarracks=BARR\nBuildWeapons=WORK\n"
        "BuildRadar=RADAR\nMaximumBaseDefenseValue=60\n"
        "[YARD]\nConstructionYard=yes\nFactory=BuildingType\nOwner=Local\nAIBuildThis=yes\nTechLevel=-1\nCost=3000\n"
        "[POWER]\nPower=100\nOwner=Local\nTechLevel=1\nCost=600\n"
        "[BARR]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=500\nPrerequisite=POWER\n"
        "[REF]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2000\nPrerequisite=POWER\n"
        "[WORK]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2000\nPrerequisite=PROC,BARR\n"
        "[RADAR]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=1000\nPrerequisite=PROC\n"
        "[SKIP]\nOwner=Local\nAIBuildThis=no\nTechLevel=1\nCost=100\n"
        "[DEF]\nOwner=Local\nIsBaseDefense=yes\nTechLevel=1\nCost=500\nPrerequisite=BARRACKS\nPrimary=GUN\n"
        "[GUN]\nDamage=10\nROF=20\nProjectile=SHOT\n[SHOT]\nAG=yes\n");
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[TeamTypes]\n0=T\n[T]\nTaskForce=TF\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    const auto count = [](const std::vector<std::string>& plan, const std::string& type) {
        return std::count(plan.begin(), plan.end(), type);
    };
    for (const auto level : {PrototypeAI::Difficulty::Easy, PrototypeAI::Difficulty::Normal, PrototypeAI::Difficulty::Hard}) {
        sw::Snapshot world;
        world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
        world.houses[0].cash = 1000000;
        world.entities = {actor(rules, 1, computer, "YARD", sw::Kind::Structure, {10,10}),
                          actor(rules, 90, player, "YARD", sw::Kind::Structure, {40,40})};
        PrototypeAI policy(rules, &ai, level);
        const auto extra = static_cast<long>(level); // 2 - AIDifficulty
        const long defenses = level == PrototypeAI::Difficulty::Hard ? 2 : level == PrototypeAI::Difficulty::Normal ? 1 : 0;
        std::uint32_t id = 2;
        std::vector<std::string> built;
        for (std::int32_t frame = 0; frame < 90 * 40; frame += 90) {
            for (const auto& order : policy.tick(world, computer, player, frame)) {
                if (order.kind != OrderKind::Build || !listed(rules, "BuildingTypes", order.type)) continue;
                world.entities.push_back(actor(rules, id, computer, order.type, sw::Kind::Structure,
                    {static_cast<std::uint16_t>(12 + id), 10}));
                ++id;
                built.push_back(order.type);
            }
        }
        const auto& plan = policy.base_plan();
        require(plan.size() >= 6 && plan[0] == "YARD" && plan[1] == "POWER" && plan[2] == "BARR" && plan[3] == "REF",
            "Base plan did not start with yard, power, barracks and refinery");
        require(count(plan, "REF") == 1 + extra && count(plan, "DEF") == defenses &&
            count(plan, "WORK") == 1 && count(plan, "RADAR") == 1 && count(plan, "SKIP") == 0 &&
            plan.size() == static_cast<std::size_t>(6 + extra + defenses),
            "Base plan did not add the difficulty refineries and defense nodes");
        require(std::find(plan.begin(), plan.end(), "WORK") > std::find(plan.begin(), plan.end(), "BARR"),
            "Base plan put the war factory before its prerequisites");
        require(count(built, "REF") == 1 + extra && count(built, "DEF") == defenses &&
            built.size() == plan.size() - 1, "Computer did not build every base plan node in turn");
    }
}

// Base 004F1190 resolves a defense node: the quarter around the yard with
// the least defense value, then the kind it needs most; an empty list
// falls back to the next. 0045EAA0 rates each defense.
void check_defense_choice() {
    const auto text = [](const std::string& cap) {
        return "[General]\nAlliedBaseDefenseCounts=1,1,1\n[Sides]\nAllies=Local\nSoviets=Enemy\n"
            "[BuildingTypes]\n0=YARD\n1=GUNS\n2=FLAK\n3=A\n4=B\n5=C\n"
            "[AI]\nBuildConst=YARD\n" + cap +
            "[A]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=100\n[B]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=100\n"
            "[C]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=100\n" +
            "[YARD]\nConstructionYard=yes\nFactory=BuildingType\nOwner=Local\nAIBuildThis=yes\nTechLevel=-1\nCost=3000\n"
            "[GUNS]\nOwner=Local\nIsBaseDefense=yes\nTechLevel=1\nCost=500\nPrimary=GUN\n"
            "[FLAK]\nOwner=Local\nIsBaseDefense=yes\nTechLevel=1\nCost=1000\nPrimary=AAGUN\n"
            "[GUN]\nDamage=10\nROF=20\nProjectile=SHOT\nWarhead=AP\n[SHOT]\nAG=yes\n"
            "[AAGUN]\nDamage=30\nROF=10\nProjectile=FLAKSHOT\nWarhead=FL\n[FLAKSHOT]\nAA=yes\nAG=no\n"
            "[AP]\nVerses=100%,100%,100%,50%,100%\n[FL]\nVerses=10%,100%,100%,100%,200%\n";
    };
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[TeamTypes]\n0=T\n[T]\nTaskForce=TF\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    // Plan: YARD, A, B, C (built), then one defense node.
    const auto first_defense = [&](const IniFile& rules, const std::vector<std::pair<std::string, ra2map::Cell>>& extra) {
        sw::Snapshot world;
        world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
        world.entities = {actor(rules, 1, computer, "YARD", sw::Kind::Structure, {20, 20}),
                          actor(rules, 90, player, "YARD", sw::Kind::Structure, {60, 60}),
                          actor(rules, 2, computer, "A", sw::Kind::Structure, {21, 21}),
                          actor(rules, 3, computer, "B", sw::Kind::Structure, {19, 21}),
                          actor(rules, 4, computer, "C", sw::Kind::Structure, {21, 19})};
        std::uint32_t id = 5;
        for (const auto& [type, cell] : extra) world.entities.push_back(actor(rules, id++, computer, type, sw::Kind::Structure, cell));
        PrototypeAI policy(rules, &ai, PrototypeAI::Difficulty::Hard);
        for (const auto& order : frames(policy, world, 0, 2))
            if (order.kind == OrderKind::Build) return order.type;
        return std::string{};
    };
    const auto rules = parse(text("MaximumBaseDefenseValue=60\n"));
    // No defenses: every need is 0.33, so the +11E0 list (AG weapons by
    // Verses[0]) is asked first: GUNS. FLAK has only an air value.
    require(first_defense(rules, {}) == "GUNS", "The first defense of an undefended base was not the ground type");
    // A GUNS in every quarter: each quarter has +11DC 20 and +11E0 20 and
    // no air value, so the north quarter (the first least one) needs air.
    require(first_defense(rules, {{"GUNS", {20, 10}}, {"GUNS", {30, 20}}, {"GUNS", {20, 30}}, {"GUNS", {10, 20}}}) == "FLAK",
        "A base with ground defense in every quarter did not add an air defense");
    // MaximumBaseDefenseValue defaults to -1 (0065B830): no type has a
    // value, so the node is removed and nothing is built.
    const auto uncapped = parse(text(""));
    const auto none_built = first_defense(uncapped, {});
    require(none_built.empty(), "A defense was chosen without MaximumBaseDefenseValue");
}

// A small base for the 004E9660 / 004E9C90 checks. Plan: YARD, POWER,
// BARR, REF, WORK, DOCK, LAB, SUPER, PLAIN (Easy, no defense nodes).
std::string base_rules_text(const std::string& extra = {}) {
    return
        "[General]\nPrerequisitePower=POWER\nPrerequisiteProc=REF\nHarvestersPerRefinery=2\nHarvesterUnit=MINER\n"
        "GDIPowerPlant=POWER\nNodRegularPower=POWER\n" + extra +
        "[Sides]\nAllies=Local\nSoviets=Enemy\n"
        "[BuildingTypes]\n0=YARD\n1=POWER\n2=BARR\n3=REF\n4=WORK\n5=DOCK\n6=LAB\n7=SUPER\n8=PLAIN\n"
        "[VehicleTypes]\n0=MINER\n"
        "[AI]\nBuildConst=YARD\nBuildPower=POWER\nBuildRefinery=REF\nBuildBarracks=BARR\nBuildWeapons=WORK\n"
        "[YARD]\nConstructionYard=yes\nFactory=BuildingType\nOwner=Local\nAIBuildThis=yes\nTechLevel=-1\nCost=3000\n"
        "[POWER]\nPower=100\nOwner=Local\nTechLevel=1\nCost=600\n"
        "[BARR]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=500\nPrerequisite=POWER\nPower=-10\n"
        "[REF]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2000\nPrerequisite=POWER\nPower=-10\n"
        "[WORK]\nFactory=UnitType\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2000\nPrerequisite=PROC,BARR\nPower=-10\n"
        "[DOCK]\nNaval=yes\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=1000\nPrerequisite=WORK\nPower=-10\n"
        "[LAB]\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=1500\nPrerequisite=WORK\nPower=-10\n"
        "[SUPER]\nSuperWeapon=SWA\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2500\nPrerequisite=LAB\nPower=-10\n"
        "[PLAIN]\nSuperWeapon=SWB\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\nCost=2500\nPrerequisite=LAB\nPower=-10\n"
        "[SWA]\nDisableableFromShell=yes\n[SWB]\nDisableableFromShell=no\n"
        "[MINER]\nHarvester=yes\nOwner=Local\nTechLevel=1\nCost=1400\nPrerequisite=WORK\n";
}
IniFile base_rules(const std::string& extra = {}) { return parse(base_rules_text(extra)); }

IniFile base_ai() {
    return parse("[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[TeamTypes]\n0=T\n[T]\nTaskForce=TF\n"
        "[AITriggerTypes]\nTR=Attack,T,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
}

sw::Snapshot base_world(const IniFile& rules) {
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.houses[0].cash = 1000000;
    world.entities = {actor(rules, 1, computer, "YARD", sw::Kind::Structure, {10,10}),
                      actor(rules, 90, player, "YARD", sw::Kind::Structure, {40,40})};
    return world;
}

std::vector<std::string> building_orders(const IniFile& rules, const std::vector<Order>& orders) {
    std::vector<std::string> result;
    for (const auto& order : orders)
        if (order.kind == OrderKind::Build && listed(rules, "BuildingTypes", order.type)) result.push_back(order.type);
    return result;
}

// Base 004E9660 removes a Naval node while House +138 is clear; 0044B480
// clears +138 when Exit_Object (00440520) finds no place for a finished
// Naval building. No new building is chosen while one is in the factory
// (+553C is -1 only after Exit_Object or 004B97C0).
void check_naval_placement_failure() {
    const auto rules = base_rules();
    const auto ai = base_ai();
    auto world = base_world(rules);
    PrototypeAI policy(rules, &ai);
    std::vector<std::string> built;
    std::uint32_t id = 2;
    std::int32_t frame = 0;
    int docks = 0;
    for (; frame < 90 * 40; frame += 90) {
        const auto orders = building_orders(rules, policy.tick(world, computer, player, frame));
        if (!orders.empty() && orders[0] == "DOCK") {
            ++docks;
            // The product waits in the factory: a Created structure.
            auto product = actor(rules, id++, computer, "DOCK", sw::Kind::Structure, {0, 0});
            product.lifetime = sw::Lifetime::Created;
            world.entities.push_back(product);
            require(building_orders(rules, policy.tick(world, computer, player, frame + 90)).empty(),
                "Computer chose a new building while one was in the factory");
            frame += 90;
            // The app found no water place: it abandons the product and reports it.
            world.entities.back().lifetime = sw::Lifetime::Destroyed;
            world.entities.back().resolved_hp = 0;
            policy.placement_failed(player, "DOCK");
            require(policy.naval_allowed(), "Placement failure of another house cleared the naval flag");
            policy.placement_failed(computer, "DOCK");
            continue;
        }
        for (const auto& type : orders) {
            world.entities.push_back(actor(rules, id, computer, type, sw::Kind::Structure,
                {static_cast<std::uint16_t>(12 + id), 10}));
            ++id;
            built.push_back(type);
        }
    }
    const auto& plan = policy.base_plan();
    require(docks == 1 && !policy.naval_allowed(), "Computer did not drop the naval yard after its placement failed");
    require(std::find(plan.begin(), plan.end(), "DOCK") == plan.end() &&
        std::find(built.begin(), built.end(), "LAB") != built.end() &&
        std::find(built.begin(), built.end(), "PLAIN") != built.end(),
        "Base plan stopped at the naval yard that could not be placed");
    policy.reset();
    require(policy.naval_allowed(), "A new match did not set House +138 again (004E18C0)");
}

// Base 004EF890 keeps a type whose TechLevel is -1 (it tests TechLevel <=
// House +11C), and 004E9660 orders a plan node without a tech test. A map
// that sets TechLevel=-1 on its shipyard still gets the shipyard built;
// the computer then drops it when it finds no water.
void check_naval_tech_level_minus_one() {
    auto text = base_rules_text();
    const std::string dock = "[DOCK]\nNaval=yes\nOwner=Local\nAIBuildThis=yes\nTechLevel=1\n";
    const auto at = text.find(dock);
    require(at != std::string::npos, "test rules lost the DOCK section");
    text.replace(at, dock.size(), "[DOCK]\nNaval=yes\nOwner=Local\nAIBuildThis=yes\nTechLevel=-1\n");
    const auto rules = parse(text);
    const auto ai = base_ai();
    auto world = base_world(rules);
    PrototypeAI policy(rules, &ai);
    std::uint32_t id = 2;
    bool ordered = false;
    for (std::int32_t frame = 0; frame < 90 * 40 && !ordered; frame += 90)
        for (const auto& type : building_orders(rules, policy.tick(world, computer, player, frame))) {
            if (type == "DOCK") { ordered = true; break; }
            world.entities.push_back(actor(rules, id, computer, type, sw::Kind::Structure,
                {static_cast<std::uint16_t>(12 + id), 10}));
            ++id;
        }
    require(ordered, "Computer did not order a plan node with TechLevel -1");
    const auto centre = policy.base_centre(computer);
    require(centre && centre->x == 10 && centre->y == 10, "House +53BC is not the construction yard Cell");
}

// Base 004EF890 and 004EF600: with the Super Weapons option (00A3D2C3)
// off, a building whose SuperWeapon is DisableableFromShell is left out.
void check_super_weapon_option() {
    const auto rules = base_rules();
    const auto ai = base_ai();
    for (const bool allowed : {true, false}) {
        auto world = base_world(rules);
        PrototypeAI policy(rules, &ai);
        require(policy.super_weapons(), "Super Weapons option is not on by default");
        policy.set_super_weapons(allowed);
        policy.tick(world, computer, player, 0);
        const auto& plan = policy.base_plan();
        const bool super = std::find(plan.begin(), plan.end(), "SUPER") != plan.end();
        const bool plain = std::find(plan.begin(), plan.end(), "PLAIN") != plan.end();
        require(super == allowed && plain, "Base plan ignored the Super Weapons option");
    }
}

// Base 004E9660 chooses the next node without a money test; the factory
// pays it in stages (004B9300). 004E9C90 keeps the money test for units.
void check_building_without_cash() {
    const auto rules = base_rules();
    const auto ai = base_ai();
    auto world = base_world(rules);
    world.houses[0].cash = 0;
    PrototypeAI policy(rules, &ai);
    const auto orders = building_orders(rules, policy.tick(world, computer, player, 0));
    require(orders.size() == 1 && orders[0] == "POWER", "Computer waited for the full building cost");
}

// Base 004E9660: a node that drains more than the free power gets a power
// node in front of it (GDIPowerPlant for side 0). 004F1190 failure removes
// a defense node.
void check_power_and_defense_nodes() {
    const auto rules = base_rules();
    const auto ai = base_ai();
    auto world = base_world(rules);
    world.entities.push_back(actor(rules, 2, computer, "POWER", sw::Kind::Structure, {14, 10}));
    world.houses[0].power_output = 100;
    world.houses[0].power_drain = 95;
    PrototypeAI policy(rules, &ai);
    require(building_orders(rules, policy.tick(world, computer, player, 0)).empty() &&
        policy.base_plan().size() > 3 && policy.base_plan()[2] == "POWER" && policy.base_plan()[3] == "BARR",
        "Low power did not put a power node in front of the next node");
    const auto orders = building_orders(rules, policy.tick(world, computer, player, 90));
    require(orders.size() == 1 && orders[0] == "POWER", "Computer did not build the inserted power node");

    // One defense node, and no base defense type: the node is removed.
    const auto defended = base_rules("AlliedBaseDefenseCounts=1,1,1\n");
    auto base = base_world(defended);
    PrototypeAI guard(defended, &ai);
    std::vector<std::string> built;
    std::uint32_t id = 2;
    for (std::int32_t frame = 0; frame < 90 * 30; frame += 90)
        for (const auto& type : building_orders(defended, guard.tick(base, computer, player, frame))) {
            base.entities.push_back(actor(defended, id, computer, type, sw::Kind::Structure,
                {static_cast<std::uint16_t>(12 + id), 10}));
            ++id;
            built.push_back(type);
        }
    const auto& plan = guard.base_plan();
    require(std::find(plan.begin(), plan.end(), std::string{}) == plan.end() &&
        std::find(built.begin(), built.end(), "PLAIN") != built.end(),
        "An unresolved defense node stopped the base plan");
}

// Base 004E9660 and 004F5D30: below AIPickWallDefensePercent the defense
// node becomes a ring of wall nodes around the last built ProtectWithWall
// building before it. The ring goes right after that building's node.
void check_wall_pick() {
    auto text = base_rules_text("AlliedBaseDefenseCounts=1,1,1\nAIPickWallDefensePercent=100,100,100\n");
    const std::string yard = "[YARD]\nConstructionYard=yes\n";
    const auto at = text.find(yard);
    require(at != std::string::npos, "test rules lost the YARD section");
    text.replace(at, yard.size(), yard + "ProtectWithWall=yes\nFoundation=2x2\n");
    text += "[WALL]\nWall=yes\nAIBasePlanningSide=1\n[WALL2]\nWall=yes\nAIBasePlanningSide=0\n";
    text.replace(text.find("[AI]\n"), 5, "[AI]\nConcreteWalls=WALL,WALL2\n");
    const auto rules = parse(text);
    const auto ai = base_ai();
    auto world = base_world(rules);
    PrototypeAI policy(rules, &ai);
    std::vector<std::string> built;
    std::uint32_t id = 2;
    for (std::int32_t frame = 0; frame < 90 * 30; frame += 90)
        for (const auto& type : building_orders(rules, policy.tick(world, computer, player, frame))) {
            world.entities.push_back(actor(rules, id, computer, type, sw::Kind::Structure,
                {static_cast<std::uint16_t>(12 + id), 10}));
            ++id;
            built.push_back(type);
        }
    const auto plan = policy.base_plan();
    const auto cells = policy.base_plan_cells();
    require(std::find(plan.begin(), plan.end(), std::string{}) == plan.end() &&
        std::find(built.begin(), built.end(), "PLAIN") != built.end() &&
        std::find(built.begin(), built.end(), "WALL2") == built.end(),
        "The wall pick did not replace the defense node, or the plan stopped");
    // YARD at (10, 10), 2x2: 12 wall Cells from (9, 9) to (12, 12), after node 0.
    require(plan.size() > 13 && plan[0] == "YARD" && !cells[0] && plan[13] != "WALL2",
        "The wall ring is not right after the protected building");
    std::set<std::pair<int, int>> ring;
    for (std::size_t i = 1; i <= 12; ++i) {
        require(plan[i] == "WALL2" && cells[i], "A wall node has the wrong type or no Cell");
        ring.insert({cells[i]->x, cells[i]->y});
    }
    require(ring.size() == 12 && !ring.count({10, 10}) && ring.count({9, 9}) && ring.count({12, 12}) &&
        ring.count({12, 9}) && ring.count({9, 12}) && ring.count({11, 9}) && ring.count({9, 11}),
        "The wall ring Cells are not the border around the 2x2 yard");
    // The corners go in last at the same place, so (12, 12) comes first.
    require(cells[1]->x == 12 && cells[1]->y == 12, "The wall nodes are not in the 004F7270 order");
}

// Base 004E9C90: the war factory makes the first owned HarvesterUnit while
// the house has fewer than HarvestersPerRefinery per refinery.
void check_harvester_ratio() {
    const auto rules = base_rules();
    const auto ai = base_ai();
    for (const int miners : {1, 2}) {
        auto world = base_world(rules);
        std::uint32_t id = 2;
        for (const auto* type : {"POWER", "BARR", "REF", "WORK"})
            world.entities.push_back(actor(rules, id++, computer, type, sw::Kind::Structure,
                {static_cast<std::uint16_t>(12 + id), 10}));
        for (int i = 0; i < miners; ++i)
            world.entities.push_back(actor(rules, id++, computer, "MINER", sw::Kind::Unit, {20, 20}));
        // 004E9C90 makes no money test for the harvester; the factory
        // (0044B480) starts a product only with at least 11 credits.
        for (const int cash : {10, 11}) {
            world.houses[0].cash = cash;
            PrototypeAI policy(rules, &ai);
            const auto orders = policy.tick(world, computer, player, 0);
            const bool harvester = std::any_of(orders.begin(), orders.end(), [](const Order& order) {
                return order.kind == OrderKind::Build && order.type == "MINER" && order.entity == 5;
            });
            require(harvester == (miners == 1 && cash == 11), "War factory did not keep two harvesters per refinery");
        }
        // House +5460 counts buildings on the map: a second refinery that
        // is still in the factory (Created) does not raise the limit.
        if (miners != 2) continue;
        auto extra = actor(rules, id++, computer, "REF", sw::Kind::Structure, {30, 30});
        extra.lifetime = sw::Lifetime::Created;
        extra.cell.reset();
        world.entities.push_back(extra);
        world.houses[0].cash = 11;
        PrototypeAI policy(rules, &ai);
        const auto orders = policy.tick(world, computer, player, 0);
        const bool harvester = std::any_of(orders.begin(), orders.end(), [](const Order& order) {
            return order.kind == OrderKind::Build && order.type == "MINER" && order.entity == 5;
        });
        require(!harvester, "A refinery in the factory raised the harvester limit");
    }
}

// Base 004EF890 with the installed rules: both sides, every difficulty.
void check_installed_base_plan(const IniFile& rules, const IniFile& ai, const Faction& allied, const Faction& soviet) {
    for (const auto* faction : {&allied, &soviet})
    for (const auto level : {PrototypeAI::Difficulty::Easy, PrototypeAI::Difficulty::Normal, PrototypeAI::Difficulty::Hard}) {
        sw::Snapshot world;
        world.houses = {house(computer, faction->country, false), house(player, "Americans", true)};
        world.entities = {actor(rules, 1, computer, faction->yard, sw::Kind::Structure, {10,10}),
                          actor(rules, 90, player, "GACNST", sw::Kind::Structure, {60,60})};
        PrototypeAI policy(rules, &ai, level);
        const auto orders = policy.tick(world, computer, player, 0);
        const auto& plan = policy.base_plan();
        const bool sov = faction == &soviet;
        const long refineries = std::count(plan.begin(), plan.end(), sov ? "NAREFN" : "GAREFN");
        const long defenses = std::count(plan.begin(), plan.end(), std::string{});
        // [General] AlliedBaseDefenseCounts=25,20,6 and SovietBaseDefenseCounts=25,22,6.
        const long expected = level == PrototypeAI::Difficulty::Hard ? 25 :
            level == PrototypeAI::Difficulty::Normal ? (sov ? 22 : 20) : 6;
        require(plan.size() > 8 && plan[0] == faction->yard && plan[1] == faction->buildings[0] &&
            plan[2] == faction->buildings[1], "Installed base plan did not open with yard, power and barracks");
        require(refineries == 1 + static_cast<long>(level) && defenses == expected,
            "Installed base plan has the wrong refinery or defense count");
        require(orders.size() == 1 && orders[0].kind == OrderKind::Build && orders[0].type == faction->buildings[0],
            "Computer did not start the plan with power");
    }
}

void check_enemy_power_triggers() {
    const auto rules = parse(yard_rules("[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"));
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.houses[1].power_drain = 100;
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}), actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    add_yard(rules, world);
    for (const auto condition : {2, 3}) {
        const auto ai = parse(
            "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[TeamTypes]\n0=T\n[T]\nTaskForce=TF\nScript=S\n"
            "[ScriptTypes]\n0=S\n[S]\n0=0,2\n[AITriggerTypes]\nTR=Power,T,<all>,0," +
            std::to_string(condition) + ",<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
        for (const auto reserve : {-1, 0, 99, 100}) {
            world.houses[1].power_output = 100 + reserve;
            PrototypeAI policy(rules, &ai);
            policy.tick(world, computer, player, at(rules, policy) + 0);
            require(!policy.teams().empty() == (reserve < (condition == 2 ? 100 : 0)),
                "Enemy power trigger used the wrong reserve threshold");
        }
    }
}

void check_defense_cap_release() {
    const auto rules = parse(yard_rules(
        "[General]\nTotalAITeamCap=1,1,1\nTeamDelays=90,90,90\n[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"));
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[ScriptTypes]\n0=S\n[S]\n0=0,2\n"
        "[TeamTypes]\n0=DEF\n1=ATT\n[DEF]\nTaskForce=TF\nScript=S\nIsBaseDefense=yes\nPriority=1\n"
        "[ATT]\nTaskForce=TF\nScript=S\nPriority=10\n"
        "[AITriggerTypes]\nD=Defense,DEF,<all>,0,-1,<none>,0,5000,1,5000,1,0,0,0,<none>,1,1,1\n"
        "A=Attack,ATT,<all>,0,-1,<none>,0,1,1,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}), actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    add_yard(rules, world);
    PrototypeAI policy(rules, &ai);
    policy.tick(world, computer, player, at(rules, policy) + 0);
    require(policy.teams().size() == 1 && policy.teams()[0].team_type == "DEF", "Defense setup failed");
    policy.tick(world, computer, player, at(rules, policy) + 90);
    require(policy.teams().size() == 1 && policy.teams()[0].team_type == "ATT" &&
        policy.teams()[0].members == std::vector<std::uint32_t>{10},
        "Team cap did not release defense members for an attack team");
}

void check_team_lifetime() {
    const auto rules = parse(yard_rules(
        "[General]\nTeamDelays=10000,10000,10000\nDissolveUnfilledTeamDelay=100\n"
        "AITriggerFailureWeightDelta=-50\n[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"
        "[GI]\nCost=100\nTechLevel=1\nOwner=Local\nPrerequisite=RADAR\n"));
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[ScriptTypes]\n0=S\n[S]\n0=0,2\n[TeamTypes]\n0=T\n"
        "[T]\nTaskForce=TF\nScript=S\nMax=1\n"
        "[AITriggerTypes]\nTR1=One,T,<all>,0,-1,<none>,0,90,10,100,1,0,0,0,<none>,1,1,1\n"
        "TR2=Two,T,<all>,0,-1,<none>,0,80,10,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    add_yard(rules, world);
    // Base 004F3630: in a skirmish an idle unit does not replace the factory.
    PrototypeAI idle(rules, &ai);
    idle.tick(world, computer, player, at(rules, idle));
    require(idle.teams().empty(), "Idle unit made a team without a factory");
    // FindFactory(..., requireCanBuild=false): prerequisites are not checked.
    world.entities.front() = actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4,4});
    PrototypeAI policy(rules, &ai);
    const auto start = at(rules, policy);
    policy.tick(world, computer, player, start);
    require(policy.teams().size() == 1 && policy.teams()[0].forming,
        "Factory without member prerequisites did not allow the team");
    // Base 006B7AD0: an empty team older than DissolveUnfilledTeamDelay is
    // deleted. 006B7770 gives the failure delta to every trigger with Team1 T.
    policy.tick(world, computer, player, start + 100);
    require(policy.teams().size() == 1, "Team dissolved before the delay passed");
    policy.tick(world, computer, player, start + 145); // The next decision frame.
    require(policy.teams().empty() && *policy.trigger_weight("TR1") == 40.0 && *policy.trigger_weight("TR2") == 30.0,
        "Unfilled team was not dissolved with the failure delta on each of its triggers");
}

// Base 006FDFC0 (the MCV deploy) sets House +13A; 006BED50 picks no team
// before it. 006B8DC0 lets a team of higher Priority (+B4) take a member
// from a team of lower Priority. 006B88B0: a moving team that falls under
// strength stops its script and goes back to an own building without a
// weapon.
void check_team_structure() {
    const auto rules = parse(
        "[General]\nTeamDelays=50,50,50\n[InfantryTypes]\n0=GI\n[BARR]\nFactory=InfantryType\n"
        "[YARD]\nConstructionYard=yes\n[GUNS]\nPrimary=Gun\n[Gun]\nDamage=10\nProjectile=P\n[P]\nAG=yes\n");
    const auto ai = parse(
        "[TaskForces]\n0=TF1\n1=TF3\n[TF1]\n0=1,GI\n[TF3]\n0=3,GI\n"
        "[ScriptTypes]\n0=S\n[S]\n0=5,100\n"
        "[TeamTypes]\n0=LOW\n1=HIGH\n2=REIN\n"
        "[LOW]\nTaskForce=TF1\nScript=S\nPriority=1\n[HIGH]\nTaskForce=TF1\nScript=S\nPriority=9\n"
        "[REIN]\nTaskForce=TF3\nScript=S\nReinforce=yes\n"
        "[AITriggerTypes]\nL=Low,LOW,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10, 10}),
        actor(rules, 5, computer, "BARR", sw::Kind::Structure, {4, 4}),
        actor(rules, 6, computer, "GUNS", sw::Kind::Structure, {30, 4}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {60, 60})};
    PrototypeAI before_yard(rules, &ai);
    frames(before_yard, world, at(rules, before_yard), at(rules, before_yard) + 2);
    require(before_yard.teams().empty(), "Computer picked a team before its yard started the house AI");
    add_yard(rules, world);
    PrototypeAI low(rules, &ai);
    frames(low, world, at(rules, low), at(rules, low) + 3);
    require(low.teams().size() == 1 && low.teams()[0].members == std::vector<std::uint32_t>{10},
        "House AI did not start with the yard");

    // A HIGH team takes the member of the LOW team.
    const auto ai_high = parse(
        "[TaskForces]\n0=TF1\n[TF1]\n0=1,GI\n[ScriptTypes]\n0=S\n[S]\n0=5,100\n"
        "[TeamTypes]\n0=LOW\n1=HIGH\n"
        "[LOW]\nTaskForce=TF1\nScript=S\nPriority=1\n[HIGH]\nTaskForce=TF1\nScript=S\nPriority=9\n"
        "[AITriggerTypes]\nL=Low,LOW,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n"
        "H=High,HIGH,<all>,0,1,BASE,0000000003000000000000000000000000000000000000000000000000000000,50,1,100,1,0,0,0,<none>,1,1,1\n");
    PrototypeAI steal(rules, &ai_high);
    const auto start = at(rules, steal);
    frames(steal, world, start, start + 3);
    require(steal.teams().size() == 1 && steal.teams()[0].team_type == "LOW", "Low team setup failed");
    world.entities.push_back(actor(rules, 7, computer, "BASE", sw::Kind::Structure, {6, 6}));
    frames(steal, world, start + 50, start + 52);
    const auto teams = steal.teams();
    const auto high = std::find_if(teams.begin(), teams.end(), [](const auto& t) { return t.team_type == "HIGH"; });
    require(high != teams.end() && high->members == std::vector<std::uint32_t>{10},
        "Higher Priority team did not take the member of a lower Priority team");
    world.entities.pop_back();

    // A Reinforce team of three is under strength with one member left.
    const auto ai_rein = parse(
        "[TaskForces]\n0=TF3\n[TF3]\n0=3,GI\n[ScriptTypes]\n0=S\n[S]\n0=5,100\n"
        "[TeamTypes]\n0=REIN\n[REIN]\nTaskForce=TF3\nScript=S\nReinforce=yes\n"
        "[AITriggerTypes]\nR=Rein,REIN,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    world.entities.push_back(actor(rules, 11, computer, "GI", sw::Kind::Infantry, {10, 11}));
    world.entities.push_back(actor(rules, 12, computer, "GI", sw::Kind::Infantry, {11, 10}));
    PrototypeAI rein(rules, &ai_rein);
    const auto rein_start = at(rules, rein);
    frames(rein, world, rein_start, rein_start + 8);
    require(rein.teams().size() == 1 && !rein.teams()[0].forming && rein.teams()[0].script_line == 0,
        "Reinforce team did not launch");
    for (auto& e : world.entities) if (e.id == 11 || e.id == 12) e.resolved_hp = 0;
    const auto orders = rein.tick(world, computer, player, rein_start + 9);
    require(rein.teams().size() == 1 && rein.teams()[0].forming && rein.teams()[0].script_line == -1,
        "Under-strength team did not stop its script");
    require(std::any_of(orders.begin(), orders.end(), [](const Order& o) {
        return o.kind == OrderKind::Move && o.entity == 10 && o.cell.x == 4 && o.cell.y == 4;
    }), "Under-strength team did not go back to the nearest own building without a weapon");
}

// Base 004E9C90 (vehicles): the forming teams' missing members are
// counted by type. With FillEarliestTeamProbability 100 the type wanted by
// the oldest team (least CreationFrame) is made; with 0 a random type of
// the list kept from the last rise of the greatest count. The choice
// (House +5540) stays until its product leaves the factory.
void check_production_choice() {
    const std::string base_text =
        "[General]\nTeamDelays=50,50,50\n[VehicleTypes]\n0=TANK\n1=JEEP\n"
        "[BuildingTypes]\n0=YARD\n1=WORKS\n2=LAB\n[YARD]\nConstructionYard=yes\n[WORKS]\nFactory=UnitType\n[LAB]\nStrength=100\n"
        "[TANK]\nCost=500\nTechLevel=1\nOwner=Local\n[JEEP]\nCost=300\nTechLevel=1\nOwner=Local\n";
    const auto ai = parse(
        "[TaskForces]\n0=TF_TANK\n1=TF_JEEP\n[TF_TANK]\n0=1,TANK\n[TF_JEEP]\n0=3,JEEP\n"
        "[ScriptTypes]\n0=S\n[S]\n0=5,100\n[TeamTypes]\n0=T_TANK\n1=T_JEEP\n"
        "[T_TANK]\nTaskForce=TF_TANK\nScript=S\n[T_JEEP]\nTaskForce=TF_JEEP\nScript=S\n"
        "[AITriggerTypes]\nA=Tank,T_TANK,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n"
        "B=Jeep,T_JEEP,<all>,0,1,LAB,0100000003000000000000000000000000000000000000000000000000000000,50,1,100,1,0,0,0,<none>,1,1,1\n");
    for (const int earliest : {100, 0}) {
        const auto rules = parse("[General]\nFillEarliestTeamProbability=" + std::to_string(earliest) + "," +
            std::to_string(earliest) + "," + std::to_string(earliest) + "\n" + base_text.substr(10));
        sw::Snapshot world;
        world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
        world.entities = {actor(rules, 20, player, "BASE", sw::Kind::Structure, {60, 60}),
            actor(rules, 5, computer, "WORKS", sw::Kind::Structure, {4, 4})};
        add_yard(rules, world);
        world.houses[0].cash = 0; // no product can be chosen yet
        PrototypeAI policy(rules, &ai);
        const auto start = at(rules, policy);
        require(policy.tick(world, computer, player, start).empty(), "Computer chose a product it cannot pay for");
        require(policy.teams().size() == 1, "T_TANK was not picked first"); // no LAB, so no T_JEEP
        world.entities.push_back(actor(rules, 6, computer, "LAB", sw::Kind::Structure, {8, 4}));
        world.houses[0].cash = 10000;
        // The next pick adds T_JEEP. Both teams then ask for vehicles.
        const auto orders = frames(policy, world, start + 50, start + 50);
        require(policy.teams().size() == 2, "Second team was not picked");
        const auto built = std::find_if(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::Build; });
        require(built != orders.end() && built->entity == 5 && built->type == (earliest == 100 ? "TANK" : "JEEP"),
            "Vehicle choice did not follow FillEarliestTeamProbability and the request counts");
    }
    // The choice stays until its product leaves the factory.
    const auto rules = parse(base_text);
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 20, player, "BASE", sw::Kind::Structure, {60, 60}),
        actor(rules, 5, computer, "WORKS", sw::Kind::Structure, {4, 4})};
    add_yard(rules, world);
    const auto one = parse(
        "[TaskForces]\n0=TF_TANK\n[TF_TANK]\n0=1,TANK\n[ScriptTypes]\n0=S\n[S]\n0=5,100\n[TeamTypes]\n0=T_TANK\n"
        "[T_TANK]\nTaskForce=TF_TANK\nScript=S\n"
        "[AITriggerTypes]\nA=Tank,T_TANK,<all>,0,-1,<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
    PrototypeAI policy(rules, &one);
    const auto start = at(rules, policy);
    auto orders = policy.tick(world, computer, player, start);
    require(orders.size() == 1 && orders[0].type == "TANK", "Tank was not chosen");
    auto product = actor(rules, 30, computer, "TANK", sw::Kind::Unit, {4, 5});
    product.lifetime = sw::Lifetime::Created;
    world.entities.push_back(product);
    require(policy.tick(world, computer, player, start + 1).empty(), "A product in the factory got a second order");
    world.entities.back().lifetime = sw::Lifetime::Placed; // It leaves the factory and joins.
    policy.tick(world, computer, player, start + 2);
    require(policy.teams()[0].members == std::vector<std::uint32_t>{30}, "Finished product did not join its team");
    orders = frames(policy, world, start + 3, start + 20);
    require(std::none_of(orders.begin(), orders.end(), [](const Order& o) { return o.kind == OrderKind::Build; }),
        "Computer made a unit no team wanted");
}

void check_team_policy() {
    const auto rules = parse(
        "[General]\nTeamDelays=300,200,100\nTotalAITeamCap=4,4,4\nMaximumAIDefensiveTeams=1,1,1\n"
        "AITriggerSuccessWeightDelta=20\nAITriggerFailureWeightDelta=-50\n"
        "[Sides]\nAllies=LocalCountry\nSoviets=OtherCountry\n"
        "[BuildingTypes]\n0=YARD\n1=BARR\n2=WORKS\n3=LAB\n[InfantryTypes]\n0=GI\n[VehicleTypes]\n0=TANK\n"
        "[YARD]\nStrength=100\nConstructionYard=yes\n[BARR]\nFactory=InfantryType\nStrength=100\n[WORKS]\nFactory=UnitType\nStrength=100\n"
        "[LAB]\nStrength=100\n"
        "[GI]\nStrength=50\nCost=100\nTechLevel=1\nOwner=LocalCountry\nPrerequisite=BARR\n"
        "[TANK]\nStrength=50\nCost=500\nTechLevel=1\nOwner=LocalCountry\nPrerequisite=WORKS\n");
    const auto ai = parse(
        "[TaskForces]\n0=TF_GI\n1=TF_TANK\n[TF_GI]\n0=2,GI\n[TF_TANK]\n0=1,TANK\n"
        "[ScriptTypes]\n0=S_ATTACK\n1=S_PATROL\n[S_ATTACK]\n0=46,3\n1=0,2\n2=49,0\n[S_PATROL]\n0=58,1\n1=5,2\n2=6,1\n"
        "[TeamTypes]\n0=T_GI\n1=T_TANK\n2=T_PATROL\n"
        "[T_GI]\nTaskForce=TF_GI\nScript=S_ATTACK\nMax=1\n[T_TANK]\nTaskForce=TF_TANK\nScript=S_ATTACK\nMax=1\n"
        "[T_PATROL]\nTaskForce=TF_GI\nScript=S_PATROL\nMax=1\nIsBaseDefense=yes\n"
        "[AITriggerTypes]\n"
        "TR_GI=GIs,T_GI,<all>,1,1,BARR,0100000003000000000000000000000000000000000000000000000000000000,50,10,100,1,0,1,0,<none>,1,1,1\n"
        "TR_LAB=Tank vs lab,T_TANK,<all>,1,0,LAB,0100000003000000000000000000000000000000000000000000000000000000,50,10,100,1,0,0,0,<none>,1,1,1\n"
        "TR_HARD=Patrol,T_PATROL,LocalCountry,1,-1,<none>,0000000000000000000000000000000000000000000000000000000000000000,5000,10,5000,1,0,1,1,<none>,0,0,1\n"
        "TR_SIDE=Wrong side,T_TANK,<all>,1,-1,<none>,0000000000000000000000000000000000000000000000000000000000000000,90,10,100,1,0,2,0,<none>,1,1,1\n"
        "TR_HOUSE=Wrong house,T_TANK,OtherCountry,1,-1,<none>,0000000000000000000000000000000000000000000000000000000000000000,90,10,100,1,0,0,0,<none>,1,1,1\n"
        "TR_TECH=Future,T_TANK,<all>,11,-1,<none>,0000000000000000000000000000000000000000000000000000000000000000,90,10,100,1,0,0,0,<none>,1,1,1\n"
        "TR_CAMPAIGN=Campaign,T_TANK,<all>,1,-1,<none>,0000000000000000000000000000000000000000000000000000000000000000,90,10,100,0,0,0,0,<none>,1,1,1\n"
        "TR_RICH=Rich enemy,T_TANK,<all>,1,4,<none>,1027000003000000000000000000000000000000000000000000000000000000,90,10,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "LocalCountry", false), house(player, "OtherCountry", true)};
    world.houses[0].cash = 150;
    world.houses[1].cash = 9999; // TR_RICH wants at least 10000.
    world.entities = {actor(rules, 10, computer, "YARD", sw::Kind::Structure, {10, 10}),
        actor(rules, 11, computer, "BARR", sw::Kind::Structure, {14, 10}),
        actor(rules, 30, player, "YARD", sw::Kind::Structure, {40, 40}),
        actor(rules, 31, player, "YARD", sw::Kind::Structure, {60, 60}),
        actor(rules, 32, player, "GI", sw::Kind::Infantry, {70, 70})};

    PrototypeAI easy(rules, &ai);
    require(easy.difficulty() == PrototypeAI::Difficulty::Easy && easy.trigger_count() == 8,
        "Optional AI.INI constructor did not default to Easy or dropped triggers");
    const auto start = at(rules, easy);
    auto orders = easy.tick(world, computer, player, start);
    auto teams = easy.teams();
    require(teams.size() == 1 && teams[0].team_type == "T_GI" && teams[0].forming,
        "Easy computer did not pick the only enabled trigger whose condition holds");
    // 004EA1B0 chose GI; the barracks (0044B480) starts it.
    require(orders.size() == 1 && orders[0].kind == OrderKind::Build && orders[0].type == "GI" && orders[0].entity == 11,
        "Forming team did not request one legal member from the barracks");
    // The choice needs cost <= Available_Money.
    {
        auto poor_world = world;
        poor_world.houses[0].cash = 99;
        PrototypeAI poor(rules, &ai);
        require(poor.tick(poor_world, computer, player, start).empty() && poor.teams().size() == 1,
            "Team production ignored the authored cost");
    }
    // An enemy lab makes TR_LAB true, but the tank needs a war factory first.
    world.entities.push_back(actor(rules, 33, player, "LAB", sw::Kind::Structure, {50, 50}));
    easy.tick(world, computer, player, start + 100);
    require(easy.teams().size() == 1, "Computer picked a team it cannot build or recruit, or picked Max=1 twice");
    world.entities.push_back(actor(rules, 12, computer, "WORKS", sw::Kind::Structure, {18, 10}));
    easy.tick(world, computer, player, start + 150);
    require(easy.teams().size() == 1, "Easy TeamDelays (third value) was not used");
    easy.tick(world, computer, player, start + 200);
    teams = easy.teams();
    require(teams.size() == 2 && teams[1].team_type == "T_TANK", "Enemy-owns condition did not pick its team");

    // Two finished GIs: one joins per frame (006B91C0), then the full team
    // regroups (IsReforming) and runs line 0, 46,3 against the lab.
    world.houses[0].cash = 0;
    world.entities.push_back(actor(rules, 50, computer, "GI", sw::Kind::Infantry, {12, 12}));
    world.entities.push_back(actor(rules, 51, computer, "GI", sw::Kind::Infantry, {13, 12}));
    easy.tick(world, computer, player, start + 245);
    require(easy.teams()[0].members.size() == 1 && easy.teams()[0].forming, "Recruit took more than one object per entry");
    orders = frames(easy, world, start + 246, start + 249);
    teams = easy.teams();
    require(!teams[0].forming && teams[0].script_line == 0 && teams[0].target == 33,
        "Full team did not run its script against the named enemy structure");
    require(std::count_if(orders.begin(), orders.end(), [](const Order& o) {
        return o.kind == OrderKind::Attack && o.target == 33 && (o.entity == 50 || o.entity == 51);
    }) == 2, "Team members did not attack the lab");
    // Member 51 leaves the map and the lab dies: line 1 attacks the
    // structure nearest to the leader.
    world.entities.back().lifetime = sw::Lifetime::Created;
    world.entities[5].resolved_hp = 0;
    orders = frames(easy, world, start + 250, start + 252);
    require(easy.teams()[0].script_line == 1 && easy.teams()[0].target == 30 &&
        std::any_of(orders.begin(), orders.end(), [](const Order& o) {
            return o.kind == OrderKind::Attack && o.entity == 50 && o.target == 30;
        }), "Team did not move to the next line and attack the closest enemy structure");
    // No structure is left: line 2 registers success and the script ends.
    world.entities[2].resolved_hp = 0;
    world.entities[3].resolved_hp = 0;
    frames(easy, world, start + 253, start + 256);
    require(easy.teams().size() == 1 && easy.teams()[0].team_type == "T_TANK" &&
        *easy.trigger_weight("TR_GI") == 70.0, "Script with action 49 did not disband the team and add the success delta");

    // TR_GI picks again. Its members die after launch: failure, clamped at the minimum.
    world.entities[2].resolved_hp = 100;
    world.entities.back().lifetime = sw::Lifetime::Placed;
    frames(easy, world, start + 300, start + 303);
    require(easy.teams().size() == 2 && !easy.teams()[1].forming, "Second GI team did not form from free units");
    world.entities[world.entities.size() - 2].resolved_hp = 0;
    world.entities.back().resolved_hp = 0;
    frames(easy, world, start + 304, start + 305);
    require(easy.teams().size() == 1 && *easy.trigger_weight("TR_GI") == 20.0, "Lost team did not add the failure delta");

    // Hard enables the 5000-weight trigger, which beats every normal weight.
    world.entities[world.entities.size() - 2].resolved_hp = 50;
    world.entities.back().resolved_hp = 50;
    for (auto& e : world.entities) if (e.id == 50 || e.id == 51) move_to(e, ra2map::Cell{30, 12});
    PrototypeAI hard(rules, &ai, PrototypeAI::Difficulty::Hard), normal(rules, &ai, PrototypeAI::Difficulty::Normal);
    const auto hard_start = at(rules, hard);
    orders = frames(hard, world, hard_start, hard_start + 4);
    require(hard.teams().size() == 1 && hard.teams()[0].team_type == "T_PATROL", "Hard did not use its urgent trigger");
    require(hard.teams()[0].script_line == 0 && std::count_if(orders.begin(), orders.end(), [](const Order& o) {
        return o.kind == OrderKind::Move && o.cell.x == 14 && o.cell.y == 10;
    }) == 2, "Patrol script did not move to the own barracks");
    frames(hard, world, hard_start + 5, hard_start + 20);
    require(hard.teams()[0].script_line == 0, "Move finished before arrival");
    for (auto& e : world.entities) if (e.id == 50 || e.id == 51) move_to(e, ra2map::Cell{15, 11});
    hard.tick(world, computer, player, hard_start + 21); // Arrived.
    hard.tick(world, computer, player, hard_start + 22);
    require(hard.teams()[0].script_line == 1, "Arrival did not start the guard line");
    // Guard 2 lasts 30 frames; then 6,1 jumps back to the first line.
    std::vector<std::int32_t> lines;
    for (auto frame = hard_start + 23; frame <= hard_start + 60; ++frame) {
        hard.tick(world, computer, player, frame);
        if (lines.empty() || lines.back() != hard.teams()[0].script_line) lines.push_back(hard.teams()[0].script_line);
    }
    require(lines.size() >= 3 && lines[0] == 1 && std::find(lines.begin(), lines.end(), 0) != lines.end() &&
        lines.back() == 1, "Jump 6,1 did not return to the first line after the guard time");
    normal.tick(world, computer, player, at(rules, normal) + 0);
    require(normal.teams().size() == 1 && normal.teams()[0].team_type != "T_PATROL",
        "Normal used a hard-only trigger");

    // Without AI.INI the old policy stays: no teams and plain attacks.
    PrototypeAI old(rules);
    require(!old.uses_ai_ini() && old.teams().empty(), "One-argument constructor changed the old policy");
}
} // namespace

int failures = 0;
template <typename F> void run_check(const char* name, F&& check) {
    try { check(); } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAILED " << name << ": " << error.what() << '\n';
    }
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: prototype_ai_test <base-game-directory>");
    ra2::fs::MixFileSystem files(argv[1]);
    files.mount_language(ra2::fs::GameVariant::RedAlert2);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Installed base RULES.INI is missing");
    IniFile rules;
    rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    const Faction allied{"Americans", "AMCV", "GACNST", "MTNK", {{"GAPOWR", "GAPILE", "GAREFN", "GAWEAP"}}};
    const Faction soviet{"Russians", "SMCV", "NACNST", "HTNK", {{"NAPOWR", "NAHAND", "NAREFN", "NAWEAP"}}};
    run_check("check_base_match", [&] { check_base_match(rules, allied); });
    run_check("check_base_match", [&] { check_base_match(rules, soviet); });
    run_check("check_rule_filters", [&] { check_rule_filters(); });
    run_check("check_clock_and_guards", [&] { check_clock_and_guards(rules, allied); });
    const auto ai_bytes = files.read_file("AI.INI");
    require(ai_bytes.has_value(), "Installed base AI.INI is missing");
    IniFile ai;
    ai.parse(std::string_view(reinterpret_cast<const char*>(ai_bytes->data()), ai_bytes->size()));
    for (const auto level : {PrototypeAI::Difficulty::Easy, PrototypeAI::Difficulty::Normal, PrototypeAI::Difficulty::Hard}) {
        run_check("check_base_teams", [&] { check_base_teams(rules, ai, allied, "Russians", soviet, level, "1"); });
        run_check("check_base_teams", [&] { check_base_teams(rules, ai, soviet, "Americans", allied, level, "2"); });
    }
    run_check("check_team_leader", [&] { check_team_leader(); });
    run_check("check_original_trigger_gates", [&] { check_original_trigger_gates(); });
    run_check("check_first_team_timer", [&] { check_first_team_timer(); });
    run_check("check_base_plan", [&] { check_base_plan(); });
    run_check("check_wall_pick", [&] { check_wall_pick(); });
    run_check("check_installed_base_plan", [&] { check_installed_base_plan(rules, ai, allied, soviet); });
    run_check("check_naval_placement_failure", [&] { check_naval_placement_failure(); });
    run_check("check_naval_tech_level_minus_one", [&] { check_naval_tech_level_minus_one(); });
    run_check("check_super_weapon_option", [&] { check_super_weapon_option(); });
    run_check("check_building_without_cash", [&] { check_building_without_cash(); });
    run_check("check_power_and_defense_nodes", [&] { check_power_and_defense_nodes(); });
    run_check("check_harvester_ratio", [&] { check_harvester_ratio(); });
    run_check("check_enemy_power_triggers", [&] { check_enemy_power_triggers(); });
    run_check("check_defense_cap_release", [&] { check_defense_cap_release(); });
    run_check("check_team_structure", [&] { check_team_structure(); });
    run_check("check_defense_choice", [&] { check_defense_choice(); });
    run_check("check_production_choice", [&] { check_production_choice(); });
    run_check("check_team_policy", [&] { check_team_policy(); });
    run_check("check_team_lifetime", [&] { check_team_lifetime(); });
    run_check("check_authored_gather_teams", [&] { check_authored_gather_teams(rules, ai, allied, soviet); });
    if (failures) throw std::runtime_error(std::to_string(failures) + " prototype AI checks failed");
    std::cout << "Prototype AI spot checks passed: both base factions, rule filters, deterministic clocks and targets, base AI.INI teams.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
