#include <algorithm>
#include "prototype_naval.h"
#include "../fs/mix_bootstrap.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ra2;
using namespace ra2::sim;
namespace sw = ra2::session::world;
using rules::IniFile;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t key(int x, int y) { return std::uint32_t(y) * 512 + std::uint32_t(x); }

const sw::Entity* entity(const sw::Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const sw::Entity& e) { return e.id == id; });
    return found == world.entities.end() ? nullptr : &*found;
}

// One land type per row: Water rows 0..9, Beach row 10, Clear rows 11..19.
// A rock island blocks (7,3) and (7,4).
sw::Snapshot fixture() {
    sw::Snapshot world;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    world.random = Random(24).state();
    world.cell_order = std::vector<std::uint32_t>{};
    for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x) {
        const int land = y < 10 ? 2 : y == 10 ? 6 : 0;
        auto& cell = world.cells[key(x, y)];
        cell.allocated = true;
        cell.heads = weapons::CellContentHeads{};
        cell.ground_occupancy_124 = 0;
        cell.cell_flag_100 = false;
        cell.terrain = original_default_terrain_cell({std::uint16_t(x), std::uint16_t(y)});
        cell.terrain->land_type = land;
        world.cell_order->push_back(key(x, y));
    }
    for (const auto& [x, y] : {std::pair{7, 3}, std::pair{7, 4}})
        world.cells.at(key(x, y)).terrain->land_type = 3; // Rock island.
    sw::House allies, soviets;
    allies.id = 1; allies.name = "Player"; allies.country = "Allies"; allies.cash = 5000;
    allies.tech_level = 10;
    soviets.id = 2; soviets.name = "Computer"; soviets.country = "Soviets"; soviets.cash = 5000;
    soviets.tech_level = 10;
    world.houses = {allies, soviets};
    world.creation_order.push_back({sw::CreatedKind::House, 1});
    world.creation_order.push_back({sw::CreatedKind::House, 2});
    return world;
}

void place_entity(sw::Snapshot& world, std::uint32_t id, std::uint32_t house,
                  const std::string& type, sw::Kind kind, int x, int y, int hp = 100) {
    sw::Entity entity;
    entity.id = id;
    entity.house_id = house;
    entity.type = type;
    entity.kind = kind;
    entity.lifetime = sw::Lifetime::Placed;
    entity.resolved_hp = hp;
    entity.maximum_hp = hp;
    entity.cell = ra2map::Cell{std::uint16_t(x), std::uint16_t(y)};
    entity.position = Coord{x * 256 + 128, y * 256 + 128, 0};
    entity.layer = sw::Layer::Ground;
    entity.next = 0;
    entity.active = true;
    entity.in_limbo = false;
    entity.on_map = true;
    world.entities.push_back(entity);
    world.creation_order.push_back({sw::CreatedKind::Entity, id});
    auto& cell = world.cells.at(key(x, y));
    cell.heads->ground_head = id;
    *cell.ground_occupancy_124 |= 0x20;
}

IniFile naval_rules() {
    IniFile rules;
    rules.parse(
        "[General]\nBuildNavalYard=NAYARD,GAYARD\nPrerequisiteTech=GATECH,NATECH\n"
        "[Clear]\nFoot=100%\nTrack=100%\nWheel=100%\nHover=50%\nFloat=0%\nAmphibious=80%\n"
        "[Water]\nFoot=0%\nTrack=0%\nWheel=0%\nHover=100%\nFloat=100%\nAmphibious=100%\n"
        "[Beach]\nFoot=0%\nTrack=0%\nWheel=0%\nHover=75%\nFloat=0%\nFloatBeach=100%\nAmphibious=60%\n"
        "[Ice]\nFoot=50%\nTrack=80%\nWheel=50%\nHover=100%\nFloat=0%\n"
        "[Rock]\nFoot=0%\nTrack=0%\nWheel=0%\nHover=0%\nFloat=0%\n"
        "[GAYARD]\nName=Allied Naval Yard\nFactory=UnitType\nNaval=yes\nWaterBound=yes\nCost=1000\n"
        "[NAYARD]\nName=Soviet Naval Yard\nFactory=UnitType\nNaval=yes\nWaterBound=yes\nCost=1000\n"
        "[GAWEAP]\nName=Allied War Factory\nFactory=UnitType\nCost=2000\n"
        "[NATECH]\nName=Soviet Tech Center\nCost=2000\n"
        "[GATECH]\nName=Allied Tech Center\nCost=2000\n"
        "[VehicleTypes]\n1=DEST\n2=SUB\n3=AEGIS\n4=DRED\n5=CARRIER\n6=DLPH\n7=SQD\n"
        "[DEST]\nName=Destroyer\nStrength=600\nCost=1000\nSpeed=6\nTechLevel=4\nSight=7\n"
        "SpeedType=Float\nNaval=yes\nSensors=yes\nSensorsSight=8\nCategory=Support\n"
        "Primary=155mm\nArmor=heavy\nOwner=Allies\nPrerequisite=GAYARD\n"
        "[SUB]\nName=Typhoon Attack Sub\nStrength=600\nCost=1000\nSpeed=4\nTechLevel=2\nSight=4\n"
        "SpeedType=Float\nNaval=yes\nUnderwater=yes\nSensors=yes\nSensorsSight=7\nCategory=AFV\n"
        "Primary=SubTorpedo\nArmor=heavy\nOwner=Soviets\nPrerequisite=NAYARD\n"
        "[AEGIS]\nName=Aegis Cruiser\nStrength=800\nCost=1200\nSpeed=4\nTechLevel=7\nSight=8\n"
        "SpeedType=Float\nNaval=yes\nCategory=AFV\nPrimary=Medusa\nArmor=light\n"
        "Owner=Allies\nPrerequisite=GAYARD,RADAR\n"
        "[DRED]\nName=Dreadnought\nStrength=800\nCost=2000\nSpeed=4\nTechLevel=6\nSight=7\n"
        "SpeedType=Float\nNaval=yes\nCategory=Support\nPrimary=DredLauncher\nArmor=heavy\n"
        "Owner=Soviets\nPrerequisite=NAYARD,NATECH\n"
        "[CARRIER]\nName=Aircraft Carrier\nStrength=800\nCost=2000\nSpeed=4\nTechLevel=7\nSight=7\n"
        "SpeedType=Float\nNaval=yes\nCategory=Support\nPrimary=HornetLauncher\nArmor=heavy\n"
        "Owner=Allies\nPrerequisite=GAYARD,TECH\n"
        "[DLPH]\nName=Dolphin\nStrength=200\nCost=500\nSpeed=8\nTechLevel=5\nSight=4\n"
        "SpeedType=Float\nNaval=yes\nUnderwater=yes\nSensors=yes\nSensorsSight=8\nCategory=AFV\n"
        "Primary=SonicZap\nArmor=light\nOwner=Allies\nPrerequisite=GAYARD,GATECH\n"
        "[SQD]\nName=Giant Squid\nStrength=200\nCost=1000\nSpeed=8\nTechLevel=9\nSight=5\n"
        "SpeedType=Float\nNaval=yes\nUnderwater=yes\nSensors=yes\nSensorsSight=8\nCategory=AFV\n"
        "Primary=SquidGrab\nArmor=light\nOwner=Soviets\nPrerequisite=NAYARD,NATECH\n");
    return rules;
}

void check_speed_types() {
    require(naval_speed_type("Float") == NavalSpeedType::Float &&
        naval_speed_type("hover") == NavalSpeedType::Hover &&
        naval_speed_type("TRACK") == NavalSpeedType::Track &&
        naval_speed_type("FloatBeach") == NavalSpeedType::FloatBeach &&
        naval_speed_type("Amphibious") == NavalSpeedType::Amphibious,
        "SpeedType names did not resolve");
    require(!naval_speed_type("Rocket") && !naval_speed_type(""),
        "Unsupported SpeedType names resolved");
}

void check_water_table(const IniFile& rules) {
    const auto table = PrototypeWaterTable::load(rules);
    require(table.cost(2, NavalSpeedType::Float) == 100 &&
        table.cost(2, NavalSpeedType::Hover) == 100 &&
        table.cost(2, NavalSpeedType::Foot) == 0 &&
        table.cost(6, NavalSpeedType::Float) == 0 &&
        table.cost(6, NavalSpeedType::Hover) == 75 &&
        table.cost(0, NavalSpeedType::Hover) == 50 &&
        table.cost(0, NavalSpeedType::Float) == 0 &&
        table.cost(8, NavalSpeedType::Hover) == 100 &&
        table.cost(8, NavalSpeedType::Float) == 0 &&
        table.cost(3, NavalSpeedType::Float) == 0,
        "Water table costs did not match the authored land sections");
    require(!table.cost(-1, NavalSpeedType::Float) && !table.cost(12, NavalSpeedType::Float) &&
        !table.cost(2, NavalSpeedType::None),
        "Water table accepted an out-of-range land type or speed");
}

void check_ship_rules(const IniFile& rules) {
    const auto dest = PrototypeShipRules::load(rules, "DEST");
    require(dest.type == "DEST" && dest.display_name == "Destroyer" &&
        dest.strength == 600 && dest.cost == 1000 && dest.rule_speed == 6 &&
        dest.tech_level == 4 && dest.sight == 7 && dest.speed_type == NavalSpeedType::Float &&
        dest.naval && !dest.underwater && dest.sensors && dest.sensors_sight == 8 &&
        dest.category == "Support" && dest.primary == "155mm" && dest.armor == "heavy",
        "Destroyer rules did not load");
    const auto sub = PrototypeShipRules::load(rules, "SUB");
    require(sub.underwater && sub.sensors && sub.sensors_sight == 7 &&
        sub.display_name == "Typhoon Attack Sub" && sub.tech_level == 2,
        "Typhoon sub rules did not load");
    const auto all = load_prototype_ship_rules(rules);
    require(all.size() == 7, "Prototype ship list changed");
    require(all[0].type == "CARRIER" && all[1].type == "DEST" && all[2].type == "SUB" &&
        all[3].type == "AEGIS" && all[4].type == "DRED" && all[5].type == "SQD" &&
        all[6].type == "DLPH", "Prototype ship order is not the VehicleTypes order");
    require(!all[3].underwater && !all[3].sensors && all[4].cost == 2000 &&
        all[6].underwater && all[5].underwater, "Submerged ship facts changed");
    bool failed = false;
    try { PrototypeShipRules::load(rules, "MTNK"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "A missing ship section was accepted");
    IniFile broken;
    broken.parse("[DEST]\nName=Destroyer\nStrength=0\nCost=1000\nSpeed=6\nNaval=yes\nSpeedType=Float\n");
    failed = false;
    try { PrototypeShipRules::load(broken, "DEST"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Invalid ship rules were accepted");
}

void check_open_cells(const sw::Snapshot& world, const IniFile& rules) {
    const auto table = PrototypeWaterTable::load(rules);
    const auto open = [&](int x, int y, NavalSpeedType speed) {
        return naval_open_cell(world, {std::uint16_t(x), std::uint16_t(y)}, speed, table, 9);
    };
    require(open(3, 3, NavalSpeedType::Float).value_or(false), "Open water was blocked");
    require(open(3, 3, NavalSpeedType::Hover).value_or(false), "Hover water was blocked");
    require(!(open(12, 12, NavalSpeedType::Float).value_or(true)), "Float crossed clear land");
    require(!(open(12, 10, NavalSpeedType::Float).value_or(true)), "Float crossed a beach");
    require(open(12, 10, NavalSpeedType::Hover).value_or(false), "Hover lost the beach");
    require(open(12, 12, NavalSpeedType::Hover).value_or(false), "Hover lost clear land");
    require(!(open(7, 3, NavalSpeedType::Float).value_or(true)), "A ship entered rock");
    require(!open(25, 25, NavalSpeedType::Float).value_or(true), "A missing cell was open");
    require(!(open(600, 3, NavalSpeedType::Float).value_or(true)), "A ship left the map");
    auto occupied = world;
    const auto open2 = [&](int x, int y, NavalSpeedType speed) {
        return naval_open_cell(occupied, {std::uint16_t(x), std::uint16_t(y)}, speed, table, 9);
    };
    place_entity(occupied, 9, 1, "DEST", sw::Kind::Unit, 4, 4);
    require(open2(4, 4, NavalSpeedType::Float).value_or(false), "A ship lost its own cell");
    place_entity(occupied, 10, 1, "DEST", sw::Kind::Unit, 5, 4);
    require(!(open2(5, 4, NavalSpeedType::Float).value_or(true)), "A ship entered an occupied cell");
    auto bridged = world;
    bridged.cells.at(key(3, 3)).cell_flag_100 = true;
    require(!(naval_open_cell(bridged, {3, 3}, NavalSpeedType::Float, table, 9).value_or(true)),
        "A ship entered a bridge cell");
}

void check_movement(const sw::Snapshot& world, const IniFile& rules) {
    const auto table = PrototypeWaterTable::load(rules);
    auto one = world, replay = world;
    PrototypeNavalMovement first, second;
    place_entity(one, 11, 1, "DEST", sw::Kind::Unit, 3, 3);
    place_entity(replay, 11, 1, "DEST", sw::Kind::Unit, 3, 3);
    auto incomplete = one;
    incomplete.setup_progress = sw::SetupProgress::PackedMapRead;
    require(first.issue(incomplete, 11, {12, 3}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::NeedsInput, "Unfinished setup accepted a naval order");
    require(first.issue(one, 11, {12, 12}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::Blocked, "A ship was sent onto clear land");
    require(first.issue(one, 11, {12, 3}, 0, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::Invalid, "A ship accepted zero speed");
    require(first.issue(one, 999, {12, 3}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::NeedsInput, "A naval order used an unknown unit");
    require(first.issue(one, 11, {12, 3}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::Complete, "A water route was rejected");
    require(second.issue(replay, 11, {12, 3}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::Complete, "Replay rejected the same water route");
    // The rock island forces the route around (7,3).
    unsigned frame = 0;
    bool passed_rock = false;
    while (first.moving(11) && frame < 400) {
        const auto step = first.tick(one, frame, table);
        const auto again = second.tick(replay, frame, table);
        require(step.status == PrototypeMoveStatus::Complete && again.status == PrototypeMoveStatus::Complete,
            step.detail.c_str());
        for (const auto id : step.moved) {
            const auto* ship = entity(one, id);
            require(ship && ship->cell && ship->position, "A moved ship lost its state");
            if (ship->cell->x > 7) passed_rock = true;
            const auto* ghost = entity(replay, id);
            require(ghost && ghost->position && ghost->position->x == ship->position->x &&
                ghost->position->y == ship->position->y && ghost->cell->x == ship->cell->x &&
                ghost->cell->y == ship->cell->y, "Naval replay diverged");
        }
        ++frame;
    }
    require(!first.moving(11), "The destroyer never arrived");
    const auto* ship = entity(one, 11);
    require(ship && ship->cell && ship->cell->x == 12 && ship->cell->y == 3 &&
        ship->position->x == 12 * 256 + 128, "The destroyer stopped short of the goal");
    require(passed_rock, "The destroyer route ignored the rock island");
    require(one.cells.at(key(12, 3)).heads->ground_head == 11 &&
        *one.cells.at(key(12, 3)).ground_occupancy_124 == 0x20 &&
        one.cells.at(key(3, 3)).heads->ground_head == 0,
        "The destroyer did not transfer its cell occupancy");

    PrototypeNavalMovement hover_move;
    // Hover craft cross the beach at 75% and clear land at 50% speed.
    auto hover = world;
    place_entity(hover, 12, 1, "HOVER", sw::Kind::Unit, 12, 12);
    require(hover_move.issue(hover, 12, {12, 5}, 100, NavalSpeedType::Hover, table)
        .status == PrototypeMoveStatus::Complete, "A hover route to water was rejected");
    int slow_frames = 0, fast_frames = 0;
    frame = 0;
    while (hover_move.moving(12) && frame < 600) {
        const auto step = hover_move.tick(hover, frame, table);
        require(step.status == PrototypeMoveStatus::Complete, step.detail.c_str());
        const auto* unit = entity(hover, 12);
        require(unit && unit->cell, "A hover unit lost its cell");
        if (unit->cell->y >= 11) ++slow_frames;
        else if (unit->cell->y <= 9) ++fast_frames;
        ++frame;
    }
    require(!hover_move.moving(12), "The hover unit never arrived");
    require(slow_frames * 5 > fast_frames * 2,  "Hover speed ignored the land cost table");
    require(hover.cells.at(key(12, 5)).heads->ground_head == 12, "The hover unit missed the water goal");

    // A second ship cannot take an occupied water cell.
    PrototypeNavalMovement crowd_move;
    auto crowded = world;
    place_entity(crowded, 13, 1, "DEST", sw::Kind::Unit, 3, 3);
    require(crowd_move.issue(crowded, 13, {4, 3}, 64, NavalSpeedType::Float, table)
        .status == PrototypeMoveStatus::Complete, "An adjacent goal was rejected");
    place_entity(crowded, 14, 1, "DEST", sw::Kind::Unit, 4, 3); // Moves in after the route.
    const auto blocked = crowd_move.tick(crowded, 0, table);
    require(std::find(blocked.blocked.begin(), blocked.blocked.end(), 13) != blocked.blocked.end(),
        "A ship entered an occupied water cell");
    require(crowded.cells.at(key(3, 3)).heads->ground_head == 13, "A blocked ship left its cell");
}

void check_visibility(const sw::Snapshot& world, const IniFile& rules) {
    auto seen = world;
    // Enemy sub at (5,5); the player tank at (6,6) has no Sensors.
    place_entity(seen, 21, 2, "SUB", sw::Kind::Unit, 5, 5);
    place_entity(seen, 22, 1, "MTNK", sw::Kind::Unit, 6, 6);
    require(prototype_sees_ship(seen, rules, 2, 21), "An own-house sub was hidden");
    require(prototype_sees_ship(seen, rules, 1, 22), "A surface unit was hidden");
    require(prototype_sees_ship(seen, rules, 2, 22), "A surface enemy was hidden");
    require(!prototype_sees_ship(seen, rules, 1, 21),
        "A tank saw a submerged sub without sensors");
    // Replace the tank with a destroyer: its SensorsSight 8 covers (5,5).
    seen.entities.erase(std::find_if(seen.entities.begin(), seen.entities.end(),
        [&](const sw::Entity& e) { return e.id == 22; }));
    place_entity(seen, 23, 1, "DEST", sw::Kind::Unit, 6, 6);
    require(prototype_sees_ship(seen, rules, 1, 21), "Sensors did not reveal a submerged sub");
    // Nine cells away is outside SensorsSight 8.
    auto far_away = seen;
    far_away.entities.back().cell = ra2map::Cell{15, 15};
    far_away.entities.back().position = Coord{15 * 256 + 128, 15 * 256 + 128, 0};
    require(!prototype_sees_ship(far_away, rules, 1, 21), "Sensors revealed beyond their sight");
    // A submerged sub sees another submerged sub (SensorsSight 7).
    place_entity(seen, 24, 1, "SUB", sw::Kind::Unit, 8, 5);
    require(prototype_sees_ship(seen, rules, 2, 24), "A sub could not see a nearby sub");
    require(prototype_sees_ship(seen, rules, 1, 24), "The destroyer did not see the second sub");
    // Destroyed units are not visible.
    auto gone = seen;
    gone.entities.front().lifetime = sw::Lifetime::Destroyed;
    require(!prototype_sees_ship(gone, rules, 1, 21), "A destroyed sub stayed visible");
}

void check_shipyard(const sw::Snapshot& world, const IniFile& rules) {
    const auto table = PrototypeWaterTable::load(rules);
    auto yard_world = world;
    place_entity(yard_world, 31, 1, "GAYARD", sw::Kind::Structure, 5, 8);
    place_entity(yard_world, 32, 1, "GAWEAP", sw::Kind::Structure, 10, 8);
    place_entity(yard_world, 33, 1, "GATECH", sw::Kind::Structure, 14, 8);
    require(is_naval_yard(rules, yard_world.entities.front()), "The naval yard was rejected");
    require(!is_naval_yard(rules, yard_world.entities.back()),
        "A war factory passed as a naval yard");
    const auto* yard = naval_yard(yard_world, rules, 1);
    require(yard && yard->id == 31, "The house naval yard was not found");
    require(!naval_yard(yard_world, rules, 2), "A house without a yard found one");
    require(naval_buildable(yard_world, rules, 1, "DEST"), "The destroyer was not buildable");
    require(naval_buildable(yard_world, rules, 1, "CARRIER"), "The carrier was not buildable");
    require(naval_buildable(yard_world, rules, 1, "DLPH"), "The dolphin was not buildable");
    require(!naval_buildable(yard_world, rules, 1, "SUB"), "Allies built a Soviet sub");
    require(!naval_buildable(yard_world, rules, 1, "DRED"), "The dreadnought skipped its tech building");
    require(!naval_buildable(yard_world, rules, 2, "DEST"), "A house without a yard built a ship");
    place_entity(yard_world, 33, 1, "GATECH", sw::Kind::Structure, 2, 8);
    require(naval_buildable(yard_world, rules, 1, "CARRIER"), "The tech prerequisite was not met");
    yard_world.houses[0].cash = 999;
    require(!naval_buildable(yard_world, rules, 1, "DEST"), "Buildable ignored the house cash");
    yard_world.houses[0].cash = 5000;
    yard_world.houses[0].tech_level = 3;
    require(!naval_buildable(yard_world, rules, 1, "DEST"), "Buildable ignored the tech level");
    yard_world.houses[0].tech_level = 10;

    // Produce ships on free water next to the yard.
    auto shipyard = world;
    place_entity(shipyard, 31, 1, "GAYARD", sw::Kind::Structure, 5, 8);
    const auto first = naval_produce(shipyard, rules, table, 1, "DEST");
    require(first && *first == 32, "The destroyer did not leave the yard");
    const auto* ship = entity(shipyard, *first);
    require(ship && ship->type == "DEST" && ship->kind == sw::Kind::Unit &&
        ship->lifetime == sw::Lifetime::Placed && ship->cell && ship->position &&
        ship->resolved_hp == 600 && shipyard.houses[0].cash == 4000,
        "The produced destroyer is wrong");
    require(ship->cell->y < 10, "The destroyer left the water");
    require(std::abs(int(ship->cell->x) - 5) <= 12 && std::abs(int(ship->cell->y) - 8) <= 12,
        "The destroyer left the yard area");
    require(shipyard.cells.at(key(ship->cell->x, ship->cell->y)).heads->ground_head == *first,
        "The produced destroyer did not take its cell");
    require(shipyard.creation_order.back().id == *first, "The ship missed the creation order");
    const auto second = naval_produce(shipyard, rules, table, 1, "DEST");
    require(second && *second != *first, "The yard reused a ship id");
    const auto* second_ship = entity(shipyard, *second);
    require(second_ship && second_ship->cell && second_ship->cell->x != ship->cell->x,
        "Two destroyers shared one water cell");
    require(shipyard.houses[0].cash == 3000, "The yard ignored the second cost");
    auto no_yard = world;
    require(!naval_produce(no_yard, rules, table, 1, "DEST"), "A house without a yard built a ship");
    auto poor = shipyard;
    poor.houses[0].cash = 10;
    require(!naval_produce(poor, rules, table, 1, "CARRIER"), "The yard ignored the ship cost");
}

void check_real_rules(const char* game_dir) {
    fs::MixFileSystem files(game_dir);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Original base RULES.INI is missing");
    IniFile rules;
    rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    const auto ships = load_prototype_ship_rules(rules);
    require(ships.size() == 7, "Base rules lost a prototype ship");
    for (const auto& ship : ships) {
        if (getenv("NAVAL_DUMP")) std::cerr<<ship.type<<" "<<ship.display_name<<" hp"<<ship.strength<<" $"<<ship.cost<<" spd"<<ship.rule_speed<<" tl"<<ship.tech_level<<" uw"<<ship.underwater<<" sen"<<ship.sensors<<"/"<<ship.sensors_sight<<" naval"<<ship.naval<<" st"<<int(ship.speed_type)<<"\n";
        require(ship.naval && ship.speed_type == NavalSpeedType::Float &&
            ship.strength > 0 && ship.cost > 0,
            "A base ship lost its naval rules");
        if (ship.type == "DEST")
            require(ship.display_name == "Destroyer" && ship.strength == 600 && ship.cost == 1000 &&
                ship.rule_speed == 6 && ship.tech_level == 4 && ship.sensors &&
                ship.sensors_sight == 8 && !ship.underwater, "Base Destroyer rules changed");
        if (ship.type == "SUB")
            require(ship.display_name == "Typhoon Attack Sub" && ship.underwater &&
                ship.sensors && ship.sensors_sight == 4 && ship.cost == 1000 &&
                ship.tech_level == 2, "Base Typhoon sub rules changed");
        if (ship.type == "AEGIS")
            require(ship.display_name == "Aegis Cruiser" && ship.cost == 1200 &&
                ship.tech_level == 7 && !ship.sensors && !ship.underwater,
                "Base Aegis rules changed");
        if (ship.type == "DRED")
            require(ship.display_name == "Dreadnought" && ship.cost == 2000 &&
                ship.tech_level == 6 && ship.strength == 800, "Base Dreadnought rules changed");
        if (ship.type == "CARRIER")
            require(ship.display_name == "Aircraft Carrier" && ship.cost == 2000 &&
                ship.tech_level == 7 && ship.strength == 800, "Base carrier rules changed");
        if (ship.type == "DLPH")
            require(ship.display_name == "Dolphin" && ship.underwater && ship.sensors &&
                ship.sensors_sight == 4 && ship.cost == 500, "Base Dolphin rules changed");
        if (ship.type == "SQD")
            require(ship.display_name == "Giant Squid" && ship.underwater && ship.sensors &&
                ship.tech_level == 9, "Base squid rules changed");
    }
    const auto table = PrototypeWaterTable::load(rules);
    require(table.cost(2, NavalSpeedType::Float) == 100 &&
        table.cost(6, NavalSpeedType::Hover) == 75 &&
        table.cost(0, NavalSpeedType::Hover) == 50 &&
        table.cost(8, NavalSpeedType::Float) == 0,
        "Base land speed table changed");
    if (getenv("NAVAL_DUMP")) { for (auto& n : rules.read_list("General","BuildNavalYard")) std::cerr<<"yard "<<n<<"\n"; for (auto n : {"GAYARD","NAYARD"}) std::cerr<<n<<" fac="<<rules.read_string(n,"Factory")<<" naval="<<rules.read_bool(n,"Naval")<<" wb="<<rules.read_bool(n,"WaterBound")<<"\n"; }
    bool allied = false, soviet = false;
    for (const auto& name : rules.read_list("General", "BuildNavalYard")) {
        if (name == "GAYARD") allied = true;
        if (name == "NAYARD") soviet = true;
    }
    (void)allied; (void)soviet; // Base RULES.INI has no General BuildNavalYard; the Factory/Naval/WaterBound test covers it.
    sw::Entity yard;
    yard.kind = sw::Kind::Structure;
    yard.type = "GAYARD";
    require(is_naval_yard(rules, yard), "The base allied naval yard was rejected");
    yard.type = "NAYARD";
    require(is_naval_yard(rules, yard), "The base soviet naval yard was rejected");
    yard.type = "GAWEAP";
    require(!is_naval_yard(rules, yard), "The base war factory passed as a naval yard");
}

} // namespace

int main(int argc, char** argv) try {
    check_speed_types();
    const auto rules = naval_rules();
    check_water_table(rules);
    check_ship_rules(rules);
    const auto world = fixture();
    check_open_cells(world, rules);
    check_movement(world, rules);
    check_visibility(world, rules);
    check_shipyard(world, rules);
    if (argc == 2) check_real_rules(argv[1]);
    std::cout << "Naval prototype passed: water cells, ship movement, submerged sight, "
                 "shipyard production, and base ship rules.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
