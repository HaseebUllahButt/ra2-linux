#include "prototype_ai.h"
#include "fs/mix_bootstrap.h"

#include <algorithm>
#include <array>
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
    std::cout << "Prototype AI spot checks passed: both base factions, rule filters, deterministic clocks and targets.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
