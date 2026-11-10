#include "prototype_psychic_sensor.h"
#include "../session/world/world_hash.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace sw = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::Coord;
using ra2::sim::PsychicGoalLookup;
using ra2::sim::PsychicThreat;
using ra2::sim::PrototypePsychicSensor;
using ra2map::Cell;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

IniFile fixture_rules() {
    IniFile rules;
    rules.parse(
        "[NAPSIS]\nName=Psychic Sensor\nStrength=600\nPsychicDetectionRadius=15\n"
        "[NAPSISPOWERED]\nName=Psychic Sensor Powered\nStrength=600\nPsychicDetectionRadius=15\n"
        "Powered=yes\n");
    return rules;
}

sw::House house(std::uint32_t id, std::optional<std::int32_t> original_index,
                std::optional<std::uint32_t> mask) {
    sw::House result;
    result.id = id;
    result.country = "Test";
    result.original_index = original_index;
    result.alliance_mask_5674 = mask;
    result.power_output = 200;
    result.power_drain = 100;
    return result;
}

sw::Snapshot make_world() {
    sw::Snapshot world;
    world.edition = sw::Edition::Base;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    for (int y = 0; y <= 60; ++y)
        for (int x = 0; x <= 60; ++x) {
            sw::CellState cell;
            cell.allocated = true;
            cell.heads.emplace();
            cell.terrain.emplace();
            cell.terrain->cell = Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            cell.terrain->floor_world_height = 0;
            world.cells.emplace(static_cast<std::uint32_t>(y) * 512 + x, cell);
        }
    // Original indices 0..2, no alliances (masks 0).
    world.houses = {house(1, 0, 0u), house(2, 1, 0u), house(3, 2, 0u)};
    return world;
}

sw::Entity& place(sw::Snapshot& world, std::uint32_t id, std::uint32_t owner,
                  const std::string& type, sw::Kind kind, Coord position) {
    sw::Entity entity;
    entity.id = id;
    entity.house_id = owner;
    entity.type = type;
    entity.kind = kind;
    entity.lifetime = sw::Lifetime::Placed;
    entity.position = position;
    entity.resolved_hp = 600;
    entity.next = 0;
    entity.active = true;
    entity.in_limbo = false;
    entity.on_map = true;
    world.entities.push_back(std::move(entity));
    world.creation_order.push_back({sw::CreatedKind::Entity, id});
    return world.entities.back();
}

Coord cell_center(int x, int y, int z = 0) {
    return Coord{x * 256 + 128, y * 256 + 128, z};
}

bool same_coord(const Coord& a, const Coord& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

const PsychicThreat* find_threat(const std::vector<PsychicThreat>& threats,
                                 std::uint32_t enemy_id) {
    const auto found = std::find_if(threats.begin(), threats.end(),
        [enemy_id](const PsychicThreat& threat) { return threat.enemy_id == enemy_id; });
    return found == threats.end() ? nullptr : &*found;
}

// 1. NAPSIS radius-15 destination threshold: a goal exactly 15 cells away is
// detected, one cell farther is not.
void check_radius_threshold() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(5, 20));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup on_edge = [](std::uint32_t) { return cell_center(5, 20); };
    // 15 cells = 3840 leptons from the sensor centre.
    auto threats = sensor.query(world, 1, on_edge);
    require(find_threat(threats, 20), "goal exactly at radius 15 must be detected");
    PsychicGoalLookup beyond = [](std::uint32_t) { return cell_center(4, 20); };
    threats = sensor.query(world, 1, beyond);
    require(!find_threat(threats, 20), "goal one cell past radius 15 must not be detected");
}

// 2. A distant enemy whose attack target sits near the sensor is detected
// with attack=true, even though its own position and goal are far away.
void check_attack_target_detected() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    auto& tank = place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(50, 50));
    tank.target_id = 30; // Distant tank attacks a building next to the sensor.
    place(world, 30, 1, "NAPSIS", sw::Kind::Structure, cell_center(22, 20));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup no_goal = [](std::uint32_t) { return std::nullopt; };
    const auto threats = sensor.query(world, 1, no_goal);
    const auto* threat = find_threat(threats, 20);
    require(threat, "distant enemy attacking near the sensor must be detected");
    require(threat->attack, "attack target detection must set attack");
    require(same_coord(threat->to, cell_center(22, 20)), "to must be the attack target position");
    require(same_coord(threat->from, cell_center(50, 50)), "from must be the enemy current position");
}

// 3. A nearby enemy whose target and goal are far outside every sensor is
// not detected. Its current position inside the radius never triggers.
void check_nearby_enemy_far_orders_not_detected() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    auto& tank = place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    tank.target_id = 30; // Target far away.
    place(world, 30, 1, "MTNK", sw::Kind::Unit, cell_center(55, 55));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup far_goal = [](std::uint32_t) { return cell_center(56, 55); };
    const auto threats = sensor.query(world, 1, far_goal);
    require(!find_threat(threats, 20),
        "enemy current position inside the radius must not trigger detection");
}

// 4. The same enemy with a movement goal inside the radius is detected with
// attack=false.
void check_goal_inside_detected() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    auto& tank = place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(50, 20));
    tank.target_id = 30; // Attack target outside every sensor.
    place(world, 30, 1, "MTNK", sw::Kind::Unit, cell_center(55, 55));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup move_in = [](std::uint32_t) { return cell_center(24, 20); };
    const auto threats = sensor.query(world, 1, move_in);
    const auto* threat = find_threat(threats, 20);
    require(threat, "move goal inside the radius must be detected");
    require(!threat->attack, "goal detection must not set attack");
    require(same_coord(threat->to, cell_center(24, 20)), "to must be the movement goal");
}

// 5. Own and allied houses are excluded. Alliance uses alliance_mask_5674
// with original_index: house 2 (original index 1) is in house 1's mask.
void check_own_and_allied_excluded() {
    const auto rules = fixture_rules();
    auto world = make_world();
    world.houses[0].alliance_mask_5674 = 0b010u; // Bit of original index 1.
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    auto& own = place(world, 20, 1, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    own.target_id = 10;
    auto& ally = place(world, 21, 2, "MTNK", sw::Kind::Unit, cell_center(22, 20));
    ally.target_id = 10;
    place(world, 22, 3, "MTNK", sw::Kind::Unit, cell_center(23, 20)); // Real enemy.
    const PrototypePsychicSensor sensor(rules);
    // Only the enemy gets a destination; own and allied orders stay invisible.
    PsychicGoalLookup enemy_goal = [](std::uint32_t id) {
        return id == 22 ? std::optional<Coord>(cell_center(20, 21)) : std::nullopt;
    };
    const auto threats = sensor.query(world, 1, enemy_goal);
    require(!find_threat(threats, 20), "own units must be excluded");
    require(!find_threat(threats, 21), "allied units must be excluded");
    require(find_threat(threats, 22), "enemy units must be detected");
}

// 5b. The alliance test is asymmetric: an enemy that marks the viewer in its
// own mask must not suppress detection. Only the viewer mask counts
// (004386FF loads the viewer into ECX and calls 004E5540).
void check_enemy_mask_does_not_suppress() {
    const auto rules = fixture_rules();
    auto world = make_world();
    world.houses[2].alliance_mask_5674 = 0b001u; // Enemy marks the viewer.
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 20, 3, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup near_goal = [](std::uint32_t) { return cell_center(21, 20); };
    const auto threats = sensor.query(world, 1, near_goal);
    require(find_threat(threats, 20),
        "the enemy mask must never suppress viewer detection");
}

// 5c. The same original_index counts as allied, as 004E5540 does.
void check_same_original_index_allied() {
    const auto rules = fixture_rules();
    auto world = make_world();
    world.houses[2].original_index = 0; // Same original index as the viewer.
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 20, 3, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup near_goal = [](std::uint32_t) { return cell_center(21, 20); };
    const auto threats = sensor.query(world, 1, near_goal);
    require(!find_threat(threats, 20),
        "the same original_index must count as allied");
}

// 6. Low power disables a Powered=yes sensor; a non-powered sensor stays on.
void check_low_power_disables_powered_sensor() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 11, 1, "NAPSISPOWERED", sw::Kind::Structure, cell_center(45, 20));
    place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(46, 20)); // Near powered sensor only.
    PsychicGoalLookup near_goal = [](std::uint32_t) { return cell_center(46, 20); };
    auto threats = PrototypePsychicSensor(rules).query(world, 1, near_goal);
    require(find_threat(threats, 20), "powered sensor must work at full power");
    world.houses[0].power_drain = 300; // Output 200 < drain 300.
    threats = PrototypePsychicSensor(rules).query(world, 1, near_goal);
    require(!find_threat(threats, 20), "low power must disable the Powered=yes sensor");
    // The plain NAPSIS sensor still sees a unit next to it.
    place(world, 21, 2, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    PsychicGoalLookup goal_21 = [](std::uint32_t id) {
        return id == 21 ? std::optional<Coord>(cell_center(20, 20)) : std::nullopt;
    };
    threats = PrototypePsychicSensor(rules).query(world, 1, goal_21);
    require(find_threat(threats, 21), "low power must not disable a Powered=no sensor");
}

// 7. A dead or sold (limbo) sensor stops detecting.
void check_dead_sensor_stops() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    auto& sensor_entity = place(world, 10, 1, "NAPSIS", sw::Kind::Structure,
        cell_center(20, 20));
    PsychicGoalLookup near_goal = [](std::uint32_t) { return cell_center(21, 20); };
    auto threats = PrototypePsychicSensor(rules).query(world, 1, near_goal);
    require(find_threat(threats, 20), "live sensor must detect");
    sensor_entity.lifetime = sw::Lifetime::Destroyed;
    threats = PrototypePsychicSensor(rules).query(world, 1, near_goal);
    require(!find_threat(threats, 20), "destroyed sensor must stop detecting");
    sensor_entity.lifetime = sw::Lifetime::Placed;
    sensor_entity.in_limbo = true; // Sold buildings leave limbo copies.
    threats = PrototypePsychicSensor(rules).query(world, 1, near_goal);
    require(!find_threat(threats, 20), "limbo sensor must stop detecting");
}

// 8. Height counts: FUN_0044DAA0 is a 3D distance. A unit 15 cells away but
// far above the sensor is outside the range.
void check_z_distance_matters() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 20, 2, "ORCA", sw::Kind::Aircraft, cell_center(5, 20, 0));
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup on_edge = [](std::uint32_t) { return cell_center(5, 20, 0); };
    require(find_threat(sensor.query(world, 1, on_edge), 20),
        "level goal at radius must be detected");
    PsychicGoalLookup high = [](std::uint32_t) { return cell_center(5, 20, 512); };
    require(!find_threat(sensor.query(world, 1, high), 20),
        "two cells of height must push the goal outside the radius");
}

// 9. The query is pure: no world mutation and no random state change.
void check_no_world_mutation() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    auto& tank = place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    tank.target_id = 10;
    const auto before = sw::world_hash(world);
    const auto before_random = world.random;
    const PrototypePsychicSensor sensor(rules);
    PsychicGoalLookup goal = [](std::uint32_t) { return cell_center(22, 20); };
    const auto threats = sensor.query(world, 1, goal);
    require(!threats.empty(), "detection must find the attacking unit");
    require(sw::world_hash(world) == before, "query must not mutate the world");
    require(world.random.first == before_random.first &&
                world.random.second == before_random.second &&
                world.random.table == before_random.table,
            "query must not touch the random state");
}

// 10. One threat per enemy, deterministic enemy id order; the smaller sensor
// id wins when two sensors see the same destination.
void check_order_and_sensor_choice() {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 40, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 11, 1, "NAPSIS", sw::Kind::Structure, cell_center(21, 20));
    auto& tank = place(world, 30, 2, "MTNK", sw::Kind::Unit, cell_center(50, 50));
    tank.target_id = 10; // Own unit next to the smaller sensor 11.
    place(world, 10, 1, "MTNK", sw::Kind::Unit, cell_center(21, 20));
    auto& tank2 = place(world, 31, 3, "MTNK", sw::Kind::Unit, cell_center(50, 51));
    tank2.target_id = 10;
    const PrototypePsychicSensor sensor(rules);
    const auto threats = sensor.query(world, 1, {});
    require(threats.size() == 2, "one threat per detected enemy");
    require(threats[0].enemy_id == 30 && threats[1].enemy_id == 31,
        "threats must be ordered by enemy id");
    require(threats[0].sensor_id == 11, "the smallest seeing sensor id must win");
    require(threats[1].sensor_id == 11, "both threats must report sensor 11");
}

// 11. The real game.exe supplies the original FUN_0044DAA0 distance table;
// detection through it matches the fixture threshold.
void check_original_distance(const std::string& exe) {
    const auto rules = fixture_rules();
    auto world = make_world();
    place(world, 10, 1, "NAPSIS", sw::Kind::Structure, cell_center(20, 20));
    place(world, 20, 2, "MTNK", sw::Kind::Unit, cell_center(5, 20));
    const PrototypePsychicSensor sensor(rules, exe);
    PsychicGoalLookup on_edge = [](std::uint32_t) { return cell_center(5, 20); };
    const auto threats = sensor.query(world, 1, on_edge);
    require(find_threat(threats, 20),
        "original distance table must detect the goal at radius 15");
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: prototype_psychic_sensor_test <base-game-directory>");
    const std::string game_dir = argv[1];
    check_radius_threshold();
    check_attack_target_detected();
    check_nearby_enemy_far_orders_not_detected();
    check_goal_inside_detected();
    check_own_and_allied_excluded();
    check_enemy_mask_does_not_suppress();
    check_same_original_index_allied();
    check_low_power_disables_powered_sensor();
    check_dead_sensor_stops();
    check_z_distance_matters();
    check_no_world_mutation();
    check_order_and_sensor_choice();
    check_original_distance(game_dir + "/game.exe");
    // A supplied but invalid executable must report failure, not fall back.
    bool threw = false;
    try {
        PrototypePsychicSensor bad(fixture_rules(), game_dir + "/missing-game.exe");
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "an invalid supplied game.exe must report failure");
    std::cout << "Psychic sensor spot checks passed: radius threshold, attack target, "
                 "goal destination, own and allied exclusion, low power, dead sensor, "
                 "height, purity, ordering and the original distance table.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
