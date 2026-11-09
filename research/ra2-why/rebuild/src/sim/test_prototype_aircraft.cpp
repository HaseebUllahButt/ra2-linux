#include <cmath>
#include "prototype_aircraft.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ra2;
using namespace ra2::sim;
namespace sw = ra2::session::world;
namespace {
void require(bool value, const std::string& text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x, int y) { return std::uint32_t(y) * 512 + std::uint32_t(x); }

std::string fixture_rules_text(int docks) {
    std::string text = "[General]\nFlightLevel=1500\nReloadRate=.3\n"
        "[ORCA]\nSpeed=14\nStrength=150\nAmmo=1\nLandable=yes\nDock=GAAIRC,AMRADR\nPrimary=Maverick\n"
        "[BEAG]\nStrength=200\nSpeed=14\nAmmo=1\nLandable=yes\nDock=GAAIRC,AMRADR\nPrimary=Maverick2\n"
        "[ZEP]\nStrength=2000\nSpeed=5\nBalloonHover=yes\nJumpjetHeight=750\nPrimary=BlimpBomb\n"
        "[JUMPJET]\nStrength=125\nSpeed=9\nJumpJet=yes\nJumpjetHeight=500\nPrimary=20mm\n"
        "[Maverick]\nDamage=150\nROF=10\nRange=6\nBurst=2\n"
        "[Maverick2]\nDamage=200\nROF=10\nRange=6\nBurst=2\n"
        "[BlimpBomb]\nDamage=250\nROF=50\nRange=1.5\n"
        "[20mm]\nDamage=25\nROF=30\nRange=5\n";
    text += "[GAAIRC]\nStrength=600\nUnitReload=yes\nNumberOfDocks=" + std::to_string(docks) + "\n";
    return text;
}

rules::IniFile fixture_rules(int docks) {
    rules::IniFile rules;
    rules.parse(fixture_rules_text(docks));
    return rules;
}

sw::Entity entity(std::uint32_t id, std::uint32_t house, std::string type, sw::Kind kind,
                  ra2map::Cell cell, int z, int hp) {
    sw::Entity result;
    result.id = id;
    result.house_id = house;
    result.type = std::move(type);
    result.kind = kind;
    result.lifetime = sw::Lifetime::Placed;
    result.cell = cell;
    result.position = Coord{int(cell.x) * 256 + 128, int(cell.y) * 256 + 128, z};
    result.resolved_hp = hp;
    result.maximum_hp = hp;
    result.active = true;
    result.in_limbo = false;
    result.on_map = true;
    result.next = 0;
    return result;
}

sw::Snapshot fixture() {
    sw::Snapshot world;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    sw::House player;
    player.id = 1;
    player.name = "P";
    player.country = "Allies";
    player.power_output = 300;
    player.power_drain = 100;
    sw::House enemy;
    enemy.id = 2;
    enemy.name = "E";
    enemy.country = "Russians";
    enemy.power_output = 300;
    enemy.power_drain = 100;
    world.houses = {player, enemy};
    for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x)
        world.cells[key(x, y)].terrain = original_default_terrain_cell(
            {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
    // Pad 10, planes 11 and 12 docked on it, enemy tank 13, Kirov 14, Rocketeer 15.
    world.entities = {
        entity(10, 1, "GAAIRC", sw::Kind::Structure, {4, 4}, 0, 600),
        entity(11, 1, "ORCA", sw::Kind::Aircraft, {4, 4}, 0, 150),
        entity(12, 1, "BEAG", sw::Kind::Aircraft, {4, 4}, 0, 200),
        entity(13, 2, "MTNK", sw::Kind::Unit, {20, 20}, 0, 400),
        entity(14, 2, "ZEP", sw::Kind::Aircraft, {30, 30}, 750, 2000),
        entity(15, 1, "JUMPJET", sw::Kind::Infantry, {10, 10}, 500, 125),
    };
    return world;
}

const sw::Entity& actor(const sw::Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const auto& e) { return e.id == id; });
    require(found != world.entities.end(), "test entity is missing");
    return *found;
}

// Tick until the aircraft reaches the phase, continuing the shared frame.
std::uint32_t frames_until(PrototypeAircraft& aircraft, sw::Snapshot& world, std::uint32_t id,
                           AircraftPhase phase, std::uint32_t& frame, std::uint32_t budget) {
    for (const std::uint32_t end = frame + budget; frame < end; ++frame) {
        const auto step = aircraft.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete, "prototype frame failed: " + step.detail);
        if (aircraft.phase(id) == phase) return frame++;
    }
    throw std::runtime_error("prototype mission did not reach its phase");
}

void run(PrototypeAircraft& aircraft, sw::Snapshot& world, std::uint32_t& frame,
         std::uint32_t count) {
    for (const std::uint32_t end = frame + count; frame < end; ++frame) {
        const auto step = aircraft.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete, "prototype frame failed: " + step.detail);
    }
}

void require_at(const sw::Snapshot& world, std::uint32_t id, int x, int y, int z) {
    const auto& found = actor(world, id);
    require(found.cell && found.position && found.cell->x == x && found.cell->y == y &&
        found.position->z == z, "prototype aircraft is not where the mission put it");
}
}

int main(int argc, char** argv) try {
    const auto rules = fixture_rules(4);
    const auto loaded = PrototypeAircraftRules::load(rules, "ORCA");
    require(loaded.family == PrototypeAircraftRules::Family::Plane && loaded.type == "ORCA" &&
        loaded.speed == 14 && loaded.ammo == 1 && loaded.strength == 150 && loaded.landable &&
        loaded.dock.size() == 2 && loaded.dock[0] == "GAAIRC" && loaded.dock[1] == "AMRADR" &&
        loaded.range == 6 && loaded.rof == 10 && loaded.damage == 300,
        "ORCA rules did not load");
    require(PrototypeAircraftRules::load(rules, "ZEP").family ==
        PrototypeAircraftRules::Family::Airship, "ZEP family did not load");
    require(PrototypeAircraftRules::load(rules, "JUMPJET").family ==
        PrototypeAircraftRules::Family::Hover, "JUMPJET family did not load");
    bool failed = false;
    try { PrototypeAircraftRules::load(rules, "MTNK"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "non-flying rules were accepted");
    failed = false;
    try { PrototypeAircraftRules::load(rules, "GAAIRC"); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "pad rules were accepted as aircraft");
    require(is_reload_pad(rules, "GAAIRC") && !is_reload_pad(rules, "MTNK"),
            "pad reload rule did not load");

    // One full Harrier cycle: take off, fly, fire, return, land, reload.
    auto world = fixture();
    PrototypeAircraft aircraft(rules);
    std::uint32_t frame = 0;
    require(aircraft.issue(world, 11, 13).status == PrototypeAircraftStatus::Complete,
            "prototype attack order failed");
    require(aircraft.phase(11) == AircraftPhase::Docked, "docked plane did not stay docked");
    frames_until(aircraft, world, 11, AircraftPhase::FlyToTarget, frame, 400);
    require_at(world, 11, 4, 4, 1500);
    frames_until(aircraft, world, 11, AircraftPhase::ReturnToPad, frame, 2000);
    require(aircraft.ammo(11) && *aircraft.ammo(11) == 0, "plane did not spend its ammo");
    frames_until(aircraft, world, 11, AircraftPhase::Docked, frame, 2000);
    require_at(world, 11, 4, 4, 0);
    require(actor(world, 13).resolved_hp && *actor(world, 13).resolved_hp == 100,
            "shot did not damage the target");
    run(aircraft, world, frame, 300);
    require(aircraft.ammo(11) && *aircraft.ammo(11) == 1, "plane did not reload on the pad");
    std::cout << "Harrier cycle: one shot of 300 damage, return, land and reload in "
              << frame << " frames\n";

    // The Kirov bombs from cruise height without landing and never docks.
    world = fixture();
    PrototypeAircraft airship(rules);
    std::uint32_t air_frame = 0;
    require(airship.issue(world, 14, 13).status == PrototypeAircraftStatus::Complete,
            "airship attack order failed");
    unsigned shots = 0;
    bool landed_low = false;
    for (; air_frame < 4000 && actor(world, 13).resolved_hp && *actor(world, 13).resolved_hp > 0;
         ++air_frame) {
        const auto step = airship.tick(world, air_frame);
        require(step.status == PrototypeAircraftStatus::Complete, "airship frame failed");
        shots += step.shots.size();
        if (airship.phase(14) && actor(world, 14).position->z < 750) landed_low = true;
    }
    require(shots == 2 && !landed_low, "airship did not bomb from cruise height");
    run(airship, world, air_frame, 10);
    require(!airship.active(14) && actor(world, 14).position->z == 750 &&
        actor(world, 13).resolved_hp && *actor(world, 13).resolved_hp == 0,
        "airship did not hold after its target died");
    std::cout << "Kirov: " << shots << " bombs from cruise height 750, no docking\n";

    // The Rocketeer hovers, moves and never lands.
    world = fixture();
    PrototypeAircraft hover(rules);
    std::uint32_t hover_frame = 0;
    require(hover.issue_move(world, 15, {20, 20}).status == PrototypeAircraftStatus::Complete,
            "hover move order failed");
    require(hover.issue(world, 15, 13).status == PrototypeAircraftStatus::Complete,
            "hover attack order failed");
    shots = 0;
    landed_low = false;
    for (; hover_frame < 4000 && actor(world, 13).resolved_hp && *actor(world, 13).resolved_hp > 0;
         ++hover_frame) {
        const auto step = hover.tick(world, hover_frame);
        require(step.status == PrototypeAircraftStatus::Complete, "hover frame failed");
        shots += step.shots.size();
        if (actor(world, 15).position->z < 500) landed_low = true;
    }
    require(shots == 16 && !landed_low, "hover infantry did not fire from cruise height");
    run(hover, world, hover_frame, 10);
    require(!hover.active(15) && actor(world, 15).position->z == 500,
            "hover infantry did not hold after its target died");
    std::cout << "Rocketeer: hovered at 500 and fired " << shots << " shots\n";

    // Dock capacity: the second plane waits for a free dock slot.
    auto capacity_world = fixture();
    const auto capacity_rules = fixture_rules(1);
    capacity_world.entities.push_back(entity(21, 1, "ORCA", sw::Kind::Aircraft, {4, 4}, 0, 150));
    capacity_world.entities.push_back(entity(22, 2, "MTNK", sw::Kind::Unit, {6, 4}, 0, 400));
    PrototypeAircraft capacity(capacity_rules);
    std::uint32_t dock_frame = 0;
    require(capacity.issue(capacity_world, 11, 13).status == PrototypeAircraftStatus::Complete &&
            capacity.issue(capacity_world, 21, 13).status == PrototypeAircraftStatus::Complete,
            "capacity attack orders failed");
    frames_until(capacity, capacity_world, 11, AircraftPhase::Docked, dock_frame, 4000);
    frames_until(capacity, capacity_world, 21, AircraftPhase::Hover, dock_frame, 4000);
    require(capacity.phase(21) == AircraftPhase::Hover && capacity.phase(11) == AircraftPhase::Docked,
            "second plane did not wait for the dock");
    require(capacity.issue(capacity_world, 11, 22).status == PrototypeAircraftStatus::Complete,
            "re-arm order failed");
    frames_until(capacity, capacity_world, 21, AircraftPhase::Docked, dock_frame, 4000);
    run(capacity, capacity_world, dock_frame, 300);
    require(capacity.ammo(21) && *capacity.ammo(21) == 1, "waiting plane did not reload");
    std::cout << "Dock capacity: two planes shared one pad with NumberOfDocks=1\n";

    // A dead target sends the empty plane home without firing.
    world = fixture();
    PrototypeAircraft dead_target(rules);
    std::uint32_t dead_frame = 0;
    require(dead_target.issue(world, 11, 13).status == PrototypeAircraftStatus::Complete,
            "attack order failed");
    run(dead_target, world, dead_frame, 150);
    auto& victim = const_cast<sw::Entity&>(actor(world, 13));
    victim.resolved_hp = 0;
    frames_until(dead_target, world, 11, AircraftPhase::Docked, dead_frame, 4000);
    require(actor(world, 13).resolved_hp && *actor(world, 13).resolved_hp == 0,
            "plane fired at a dead target");
    std::cout << "Dead target: the plane returned without firing\n";

    // A dead pad makes the returning plane hover instead of crashing.
    world = fixture();
    PrototypeAircraft dead_pad(rules);
    std::uint32_t pad_frame = 0;
    require(dead_pad.issue(world, 11, 13).status == PrototypeAircraftStatus::Complete,
            "attack order failed");
    frames_until(dead_pad, world, 11, AircraftPhase::ReturnToPad, pad_frame, 2000);
    auto& pad = const_cast<sw::Entity&>(actor(world, 10));
    pad.resolved_hp = 0;
    frames_until(dead_pad, world, 11, AircraftPhase::Hover, pad_frame, 4000);
    require(dead_pad.active(11) && actor(world, 11).position->z == 1500,
            "hovering plane did not hold flight level");
    std::cout << "Dead pad: the plane circled at flight level\n";

    // Frame discipline and atomic NeedsInput frames.
    world = fixture();
    PrototypeAircraft discipline(rules);
    require(discipline.issue(world, 11, 13).status == PrototypeAircraftStatus::Complete,
            "attack order failed");
    require(discipline.tick(world, 0).status == PrototypeAircraftStatus::Complete,
            "first frame failed");
    require(discipline.tick(world, 0).status == PrototypeAircraftStatus::Invalid,
            "repeated frame was accepted");
    require(discipline.tick(world, 4294967295u).status == PrototypeAircraftStatus::Invalid,
            "rewound frame was accepted");
    auto atomic = fixture();
    PrototypeAircraft atomic_aircraft(rules);
    require(atomic_aircraft.issue(atomic, 11, 13).status == PrototypeAircraftStatus::Complete,
            "attack order failed");
    atomic.cells.at(key(4, 4)).terrain->floor_world_height.reset();
    const auto old = atomic.entities[1].position;
    require(atomic_aircraft.tick(atomic, 0).status == PrototypeAircraftStatus::NeedsInput,
            "unresolved pad floor was accepted");
    require(atomic.entities[1].position && atomic.entities[1].position->x == old->x &&
            atomic.entities[1].position->y == old->y && atomic.entities[1].position->z == old->z,
            "failed frame changed the world");
    std::cout << "Frame discipline: repeats fail and NeedsInput frames stay atomic\n";

    // Two instances replay the same mission together.
    auto a = fixture(), b = fixture();
    PrototypeAircraft left(rules), right(rules);
    require(left.issue(a, 11, 13).status == PrototypeAircraftStatus::Complete &&
            right.issue(b, 11, 13).status == PrototypeAircraftStatus::Complete,
            "replay attack orders failed");
    for (std::uint32_t replay = 0; replay < 4000 && !(replay > 50 && left.phase(11) == AircraftPhase::Docked && left.ammo(11) == 1); ++replay) {
        const auto one = left.tick(a, replay), two = right.tick(b, replay);
        require(one.status == PrototypeAircraftStatus::Complete &&
                two.status == PrototypeAircraftStatus::Complete, "replay frame failed");
        require(a.entities[1].position->x == b.entities[1].position->x &&
                a.entities[1].position->y == b.entities[1].position->y &&
                a.entities[1].position->z == b.entities[1].position->z &&
                a.entities[1].cell->x == b.entities[1].cell->x &&
                a.entities[1].cell->y == b.entities[1].cell->y, "replay differs");
    }
    require(left.phase(11) == AircraftPhase::Docked && left.ammo(11) == 1, "replay mission did not finish");
    std::cout << "Replay: two instances flew the same mission together\n";

    // Bad orders fail cleanly and cancel stops the mission.
    require(aircraft.issue(world, 0, 13).status == PrototypeAircraftStatus::Invalid,
            "zero aircraft id was accepted");
    world = fixture();
    world.entities[3].resolved_hp = 0;
    require(aircraft.issue(world, 11, 11).status == PrototypeAircraftStatus::Invalid,
            "self target was accepted");
    require(aircraft.issue(world, 11, 13).status == PrototypeAircraftStatus::Invalid,
            "dead target was accepted");
    require(aircraft.issue_move(world, 11, {8, 8}).status == PrototypeAircraftStatus::Invalid,
            "plane move order was accepted");
    require(aircraft.issue_move(world, 14, {8, 8}).status == PrototypeAircraftStatus::Invalid,
            "airship move order was accepted");
    world = fixture();
    PrototypeAircraft cancel_test(rules);
    require(cancel_test.issue(world, 11, 13).status == PrototypeAircraftStatus::Complete,
            "attack order failed");
    cancel_test.cancel(world, 11);
    require(!cancel_test.active(11) && world.entities[1].locomotor_moving == false,
            "cancel did not stop the mission");
    std::cout << "Bad orders and cancel behave\n";

    if (argc != 2) throw std::runtime_error("usage: prototype_aircraft_test <game-dir>");
    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Original base RULES.INI is missing");
    rules::IniFile original;
    original.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    PrototypeAircraft original_aircraft(original);
    require(original_aircraft.flight_level() == 1500 && std::abs(original_aircraft.reload_rate() - 0.3) < 1e-6,
            "original general aircraft rules changed");
    const auto harrier = PrototypeAircraftRules::load(original, "ORCA");
    require(harrier.family == PrototypeAircraftRules::Family::Plane && harrier.speed == 14 &&
        harrier.ammo == 1 && harrier.strength == 150 && harrier.landable &&
        harrier.dock.size() == 2 && harrier.range == 6 && harrier.rof == 10 && harrier.damage == 300,
        "base Harrier rules changed");
    const auto eagle = PrototypeAircraftRules::load(original, "BEAG");
    require(eagle.family == PrototypeAircraftRules::Family::Plane && eagle.ammo == 1 &&
        eagle.speed == 14 && eagle.dock.size() == 2 && eagle.damage == 400,
        "base Black Eagle rules changed");
    const auto kirov = PrototypeAircraftRules::load(original, "ZEP");
    require(kirov.family == PrototypeAircraftRules::Family::Airship && kirov.speed == 5 &&
        kirov.ammo == -1 && kirov.hover_height == 750 && kirov.range == 1.5 && kirov.rof == 50 &&
        kirov.damage == 250, "base Kirov rules changed");
    const auto rocketeer = PrototypeAircraftRules::load(original, "JUMPJET");
    require(rocketeer.family == PrototypeAircraftRules::Family::Hover && rocketeer.speed == 9 &&
        rocketeer.hover_height == 500 && rocketeer.range == 5 && rocketeer.rof == 30 &&
        rocketeer.damage == 25, "base Rocketeer rules changed");
    require(is_reload_pad(original, "GAAIRC") && !is_reload_pad(original, "MTNK"),
            "base pad reload rules changed");
    std::cout << "Base rules passed: ORCA, BEAG, ZEP and JUMPJET loaded from RULES.INI\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
