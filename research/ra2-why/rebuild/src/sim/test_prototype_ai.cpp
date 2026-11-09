#include "prototype_ai.h"
#include "fs/mix_bootstrap.h"

#include <algorithm>
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
    const auto first = policy.tick(world, computer, player, 0);
    require(same_orders(first, replay.tick(world, computer, player, 0)), "Team choice was not deterministic");
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
        require(std::find(wanted.begin(), wanted.end(), order.type) != wanted.end(),
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

    // Completed products appear as free units. The next decision recruits and launches.
    id = 100;
    for (const auto& team : teams)
        for (const auto& [count, type] : task_force(ai, team.team_type))
            for (int i = 0; i < count; ++i)
                world.entities.push_back(actor(rules, id++, computer, type, kind_of(rules, type), {14, 14}));
    const auto launch = policy.tick(world, computer, player, 45);
    std::map<std::string, double> weights;
    std::map<std::string, int> launched;
    for (const auto& team : policy.teams()) {
        weights[team.trigger] = *policy.trigger_weight(team.trigger);
        if (team.forming) continue;
        ++launched[team.trigger];
        for (const auto member : team.members)
            require(std::any_of(launch.begin(), launch.end(), [&](const Order& o) {
                return o.entity == member && (o.kind == OrderKind::Attack || o.kind == OrderKind::Move);
            }), "Launched team member got no script order");
    }
    require(!launched.empty(), "Full base team did not launch");
    for (auto& entity : world.entities) if (entity.id >= 100) entity.lifetime = sw::Lifetime::Destroyed;
    policy.tick(world, computer, player, 90);
    for (const auto& [trigger, count] : launched) {
        const auto f = fields(ai.read_string("AITriggerTypes", trigger));
        double expected = weights[trigger];
        for (int i = 0; i < count; ++i) expected = std::max(std::stod(f[8]), expected - 50.0);
        require(std::abs(*policy.trigger_weight(trigger) - expected) < 1e-9,
            "Destroyed team did not lower its trigger weight by the rules failure delta");
    }
}

IniFile parse(const std::string& text) {
    IniFile result;
    result.parse(text);
    return result;
}

void check_team_leader() {
    // Base 006BAA60 chooses the highest LeadershipRating before target search.
    const auto rules = parse(
        "[General]\nTeamDelays=100,100,100\n"
        "[InfantryTypes]\n0=GI\n1=OFFICER\n"
        "[GI]\nLeadershipRating=1\n[OFFICER]\nLeadershipRating=9\n");
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
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {12,12}),
        actor(rules, 21, player, "BASE", sw::Kind::Structure, {82,82})};
    PrototypeAI policy(rules, &ai);
    const auto orders = policy.tick(world, computer, player, 0);
    require(orders.size() == 2 && orders[0].target == 21 && orders[1].target == 21,
        "Team target search did not use the highest LeadershipRating");
}

void check_original_trigger_gates() {
    const auto rules = parse(
        "[General]\nTeamDelays=90,90,90\nUseMinDefenseRule=yes\n"
        "MinimumAIDefensiveTeams=1,1,1\nMaximumAIDefensiveTeams=0,0,0\n"
        "[InfantryTypes]\n0=GI\n[VehicleTypes]\n0=TANK\n");
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
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    // 0041C990: the minimum defense rule rejects offense before weighted choice.
    const auto offense = parse(records + "[T]\nTaskForce=TF\nScript=S\nMax=5\n" + trigger("<none>"));
    PrototypeAI offensive(rules, &offense);
    offensive.tick(world, computer, player, 0);
    require(offensive.teams().empty(), "Attack team bypassed the minimum defense rule");
    const auto no_min_rules = parse("[InfantryTypes]\n0=GI\n");
    auto idle_world = world;
    idle_world.entities.pop_back();
    PrototypeAI idle(no_min_rules, &offense);
    idle.tick(idle_world, computer, player, 0);
    require(idle.teams().empty(), "AI formed an attack team before the player deployed a base");
    idle_world.entities.push_back(world.entities.back());
    idle.tick(idle_world, computer, player, 2000);
    require(idle.teams().size() == 1, "Player base did not enable enemy-dependent team creation");
    // 0041C990: both task forces must be available, even if the first is defense.
    const auto paired = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=5\nIsBaseDefense=yes\n"
        "[T2]\nTaskForce=TF2\nScript=S\nMax=5\nIsBaseDefense=yes\n" + trigger("T2"));
    PrototypeAI pair(rules, &paired);
    pair.tick(world, computer, player, 0);
    require(pair.teams().empty(), "Trigger created only the available half of a team pair");
    // Defense comes from TeamType, rather than the trigger's field 13.
    const auto defense = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=5\nIsBaseDefense=yes\n" + trigger("<none>"));
    PrototypeAI defensive(rules, &defense);
    defensive.tick(world, computer, player, 0);
    defensive.tick(world, computer, player, 90);
    require(defensive.teams().size() == 1, "Defense limit used the trigger flag instead of the team flag");
    // A negative Max is unlimited in 0041C990.
    const auto unlimited = parse(records +
        "[T]\nTaskForce=TF\nScript=S\nMax=-1\nIsBaseDefense=yes\n" + trigger("<none>"));
    PrototypeAI no_limit(rules, &unlimited);
    no_limit.tick(world, computer, player, 0);
    require(no_limit.teams().size() == 1, "Negative team Max prevented all creation");
}

void check_enemy_power_triggers() {
    const auto rules = parse("[InfantryTypes]\n0=GI\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.houses[1].power_drain = 100;
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    for (const auto condition : {2, 3}) {
        const auto ai = parse(
            "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[TeamTypes]\n0=T\n[T]\nTaskForce=TF\nScript=S\n"
            "[ScriptTypes]\n0=S\n[S]\n0=0,2\n[AITriggerTypes]\nTR=Power,T,<all>,0," +
            std::to_string(condition) + ",<none>,0,50,1,100,1,0,0,0,<none>,1,1,1\n");
        for (const auto reserve : {-1, 0, 99, 100}) {
            world.houses[1].power_output = 100 + reserve;
            PrototypeAI policy(rules, &ai);
            policy.tick(world, computer, player, 0);
            require(!policy.teams().empty() == (reserve < (condition == 2 ? 100 : 0)),
                "Enemy power trigger used the wrong reserve threshold");
        }
    }
}

void check_defense_cap_release() {
    const auto rules = parse(
        "[General]\nTotalAITeamCap=1,1,1\nTeamDelays=90,90,90\n[InfantryTypes]\n0=GI\n");
    const auto ai = parse(
        "[TaskForces]\n0=TF\n[TF]\n0=1,GI\n[ScriptTypes]\n0=S\n[S]\n0=0,2\n"
        "[TeamTypes]\n0=DEF\n1=ATT\n[DEF]\nTaskForce=TF\nScript=S\nIsBaseDefense=yes\nPriority=1\n"
        "[ATT]\nTaskForce=TF\nScript=S\nPriority=10\n"
        "[AITriggerTypes]\nD=Defense,DEF,<all>,0,-1,<none>,0,5000,1,5000,1,0,0,0,<none>,1,1,1\n"
        "A=Attack,ATT,<all>,0,-1,<none>,0,1,1,100,1,0,0,0,<none>,1,1,1\n");
    sw::Snapshot world;
    world.houses = {house(computer, "Local", false), house(player, "Enemy", true)};
    world.entities = {actor(rules, 10, computer, "GI", sw::Kind::Infantry, {10,10}),
        actor(rules, 20, player, "BASE", sw::Kind::Structure, {40,40})};
    PrototypeAI policy(rules, &ai);
    policy.tick(world, computer, player, 0);
    require(policy.teams().size() == 1 && policy.teams()[0].team_type == "DEF", "Defense setup failed");
    policy.tick(world, computer, player, 90);
    require(policy.teams().size() == 1 && policy.teams()[0].team_type == "ATT" &&
        policy.teams()[0].members == std::vector<std::uint32_t>{10},
        "Team cap did not release defense members for an attack team");
}

void check_team_policy() {
    const auto rules = parse(
        "[General]\nTeamDelays=300,200,100\nTotalAITeamCap=4,4,4\nMaximumAIDefensiveTeams=1,1,1\n"
        "AITriggerSuccessWeightDelta=20\nAITriggerFailureWeightDelta=-50\n"
        "[Sides]\nAllies=LocalCountry\nSoviets=OtherCountry\n"
        "[BuildingTypes]\n0=YARD\n1=BARR\n2=WORKS\n3=LAB\n[InfantryTypes]\n0=GI\n[VehicleTypes]\n0=TANK\n"
        "[YARD]\nStrength=100\n[BARR]\nFactory=InfantryType\nStrength=100\n[WORKS]\nFactory=UnitType\nStrength=100\n"
        "[LAB]\nStrength=100\n"
        "[GI]\nStrength=50\nCost=100\nTechLevel=1\nOwner=LocalCountry\nPrerequisite=BARR\n"
        "[TANK]\nStrength=50\nCost=500\nTechLevel=1\nOwner=LocalCountry\nPrerequisite=WORKS\n");
    const auto ai = parse(
        "[TaskForces]\n0=TF_GI\n1=TF_TANK\n[TF_GI]\n0=2,GI\n[TF_TANK]\n0=1,TANK\n"
        "[ScriptTypes]\n0=S_ATTACK\n1=S_PATROL\n[S_ATTACK]\n0=54,0\n1=46,3\n2=0,2\n[S_PATROL]\n0=58,1\n1=5,2\n2=6,1\n"
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
    auto orders = easy.tick(world, computer, player, 0);
    auto teams = easy.teams();
    require(teams.size() == 1 && teams[0].team_type == "T_GI" && teams[0].forming,
        "Easy computer did not pick the only enabled trigger whose condition holds");
    require(orders.size() == 1 && orders[0].kind == OrderKind::Build && orders[0].type == "GI" && orders[0].entity == 11,
        "Forming team did not request one legal member from the barracks");
    world.houses[0].cash = 99;
    require(easy.tick(world, computer, player, 90).empty(), "Team production ignored the authored cost");
    require(easy.teams().size() == 1, "Max=1 team type was picked twice");

    // An enemy lab makes TR_LAB true, but the tank needs a war factory first.
    world.entities.push_back(actor(rules, 33, player, "LAB", sw::Kind::Structure, {50, 50}));
    easy.tick(world, computer, player, 100);
    require(easy.teams().size() == 1, "Computer picked a team it cannot build or recruit");
    world.entities.push_back(actor(rules, 12, computer, "WORKS", sw::Kind::Structure, {18, 10}));
    easy.tick(world, computer, player, 150);
    require(easy.teams().size() == 1, "Easy TeamDelays (third value) was not used");
    easy.tick(world, computer, player, 200);
    teams = easy.teams();
    require(teams.size() == 2 && teams[1].team_type == "T_TANK", "Enemy-owns condition did not pick its team");

    // Finished GIs are recruited. Line 0 (54) is skipped, line 1 attacks the lab.
    world.houses[0].cash = 0;
    world.entities.push_back(actor(rules, 50, computer, "GI", sw::Kind::Infantry, {12, 12}));
    world.entities.push_back(actor(rules, 51, computer, "GI", sw::Kind::Infantry, {13, 12}));
    orders = easy.tick(world, computer, player, 245);
    require(orders.size() == 2 && orders[0].kind == OrderKind::Attack && orders[0].entity == 50 &&
        orders[0].target == 33 && orders[1].entity == 51 && orders[1].target == 33,
        "Full team did not run its script against the named enemy structure");
    require(!easy.teams()[0].forming && easy.teams()[0].script_line == 1, "Team did not launch on line 1");
    world.entities.back().lifetime = sw::Lifetime::Created; // Member 51 leaves the map.
    world.entities[5].resolved_hp = 0;                      // The lab dies.
    orders = easy.tick(world, computer, player, 290);
    require(orders.size() == 1 && orders[0].entity == 50 && orders[0].target == 30,
        "Team did not move to the next line and attack the closest enemy structure");
    world.entities[2].resolved_hp = 0;
    world.entities[3].resolved_hp = 0;
    easy.tick(world, computer, player, 335);
    require(easy.teams().size() == 1 && easy.teams()[0].team_type == "T_TANK" &&
        *easy.trigger_weight("TR_GI") == 70.0, "Finished script did not disband the team and add the success delta");

    // TR_GI picks again. Its members die after launch: failure, clamped at the minimum.
    world.entities[2].resolved_hp = 100;
    world.houses[0].cash = 0;
    world.entities.back().lifetime = sw::Lifetime::Placed;
    easy.tick(world, computer, player, 435);
    require(easy.teams().size() == 2 && !easy.teams()[1].forming, "Second GI team did not form from free units");
    world.entities[world.entities.size() - 2].resolved_hp = 0;
    world.entities.back().resolved_hp = 0;
    easy.tick(world, computer, player, 480);
    require(easy.teams().size() == 1 && *easy.trigger_weight("TR_GI") == 20.0, "Lost team did not add the failure delta");

    // Hard enables the 5000-weight trigger, which beats every normal weight.
    world.entities[world.entities.size() - 2].resolved_hp = 50;
    world.entities.back().resolved_hp = 50;
    for (auto& e : world.entities) if (e.id == 50 || e.id == 51) e.cell = ra2map::Cell{30, 12};
    PrototypeAI hard(rules, &ai, PrototypeAI::Difficulty::Hard), normal(rules, &ai, PrototypeAI::Difficulty::Normal);
    orders = hard.tick(world, computer, player, 0); // Free GIs fill the team at once.
    require(hard.teams().size() == 1 && hard.teams()[0].team_type == "T_PATROL", "Hard did not use its urgent trigger");
    require(orders.size() == 2 && orders[0].kind == OrderKind::Move && orders[0].cell.x == 14 &&
        orders[0].cell.y == 10, "Patrol script did not move to the own barracks");
    require(hard.tick(world, computer, player, 45).empty() && hard.teams()[0].script_line == 0,
        "Move finished before arrival");
    for (auto& e : world.entities) if (e.id == 50 || e.id == 51) e.cell = ra2map::Cell{15, 11};
    hard.tick(world, computer, player, 90); // Arrived: guard 2 seconds.
    require(hard.teams()[0].script_line == 1, "Arrival did not start the guard line");
    orders = hard.tick(world, computer, player, 135);
    require(hard.teams()[0].script_line == 0 && orders.size() == 2 && orders[0].kind == OrderKind::Move,
        "Jump 6,1 did not return to the first line");
    normal.tick(world, computer, player, 0);
    require(normal.teams().size() == 1 && normal.teams()[0].team_type != "T_PATROL",
        "Normal used a hard-only trigger");

    // Without AI.INI the old policy stays: no teams and plain attacks.
    PrototypeAI old(rules);
    require(!old.uses_ai_ini() && old.teams().empty(), "One-argument constructor changed the old policy");
}
} // namespace

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
    const Faction allied{"Americans", "AMCV", "GACNST", "MTNK", {{"GAPOWR", "GAREFN", "GAPILE", "GAWEAP"}}};
    const Faction soviet{"Russians", "SMCV", "NACNST", "HTNK", {{"NAPOWR", "NAREFN", "NAHAND", "NAWEAP"}}};
    check_base_match(rules, allied);
    check_base_match(rules, soviet);
    check_rule_filters();
    check_clock_and_guards(rules, allied);
    const auto ai_bytes = files.read_file("AI.INI");
    require(ai_bytes.has_value(), "Installed base AI.INI is missing");
    IniFile ai;
    ai.parse(std::string_view(reinterpret_cast<const char*>(ai_bytes->data()), ai_bytes->size()));
    for (const auto level : {PrototypeAI::Difficulty::Easy, PrototypeAI::Difficulty::Normal, PrototypeAI::Difficulty::Hard}) {
        check_base_teams(rules, ai, allied, "Russians", soviet, level, "1");
        check_base_teams(rules, ai, soviet, "Americans", allied, level, "2");
    }
    check_team_leader();
    check_original_trigger_gates();
    check_enemy_power_triggers();
    check_defense_cap_release();
    check_team_policy();
    std::cout << "Prototype AI spot checks passed: both base factions, rule filters, deterministic clocks and targets, base AI.INI teams.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
