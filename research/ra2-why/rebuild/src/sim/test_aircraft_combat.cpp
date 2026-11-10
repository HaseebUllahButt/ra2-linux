// Part 4 integration test: PrototypeAircraft flight cycles driven through the
// shared PrototypeCombatRuntime, with the real base RULES.INI and ART.INI.
// Planes (ORCA, BEAG) fly to the target, fire exactly one ammo point through
// combat events, return to the owned pad, reload, and fire a second shot.
// ZEP and JUMPJET fire from cruise height and never land.
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "damage_runtime.h"
#include "prototype_aircraft.h"

namespace {
using namespace ra2;
using namespace ra2::sim;
namespace sw = ra2::session::world;

void require(bool value, const std::string& text) {
    if (!value) throw std::runtime_error(text);
}

std::uint32_t key(int x, int y) { return std::uint32_t(y) * 512 + std::uint32_t(x); }

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
    result.hull_facing_raw = 0;
    result.turret_facing_raw = 0;
    result.weapon_timer_start_frame = -1;
    result.weapon_timer_duration = 0;
    result.burst_index = 0;
    result.armor_multiplier = 1;
    result.house_armor_multiplier = 1;
    result.firepower_multiplier = 1;
    result.collision_eligible = true;
    result.collision_virtual_position = result.position;
    return result;
}

sw::Snapshot fixture() {
    sw::Snapshot world;
    world.setup_progress = sw::SetupProgress::PrototypeComplete;
    world.random = sim::Random(0).state();
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
    // The enemy house is human controlled, so the 006CDC40 retaliation stays
    // off and the tank never shoots back in this one-sided test.
    enemy.human_controlled = true;
    world.houses = {player, enemy};
    for (int y = 0; y <= 40; ++y) for (int x = 0; x <= 40; ++x)
        world.cells[key(x, y)].terrain =
            original_default_terrain_cell({static_cast<std::uint16_t>(x),
                                           static_cast<std::uint16_t>(y)});
    world.entities = {
        entity(10, 1, "GAAIRC", sw::Kind::Structure, {4, 4}, 0, 600),
        entity(11, 1, "ORCA", sw::Kind::Aircraft, {4, 4}, 0, 150),
        entity(12, 1, "BEAG", sw::Kind::Aircraft, {4, 4}, 0, 200),
        entity(13, 2, "HTNK", sw::Kind::Unit, {20, 20}, 0, 10000),
        entity(14, 2, "ZEP", sw::Kind::Unit, {30, 30}, 750, 2000),
        entity(15, 1, "JUMPJET", sw::Kind::Infantry, {10, 10}, 500, 125),
    };
    // Link the pad and the ground actors into their cells like the original.
    world.cells.at(key(4, 4)).heads.emplace().ground_head = 10;
    world.cells.at(key(20, 20)).heads.emplace().ground_head = 13;
    world.cells.at(key(20, 20)).ground_occupancy_124 = 0x20;
    world.cells.at(key(30, 30)).heads.emplace().ground_head = 14;
    world.cells.at(key(10, 10)).heads.emplace().ground_head = 15;
    return world;
}

std::size_t count_events(const PrototypeCombatTick& tick, PrototypeCombatEventKind kind,
                         std::uint32_t id, bool by_source) {
    std::size_t count = 0;
    for (const auto& event : tick.events)
        if (event.kind == kind && (by_source ? event.source_id : event.target_id) == id) ++count;
    return count;
}

const sw::Entity& actor(const sw::Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const auto& e) { return e.id == id; });
    require(found != world.entities.end(), "test entity is missing");
    return *found;
}

std::string diagnostics(const PrototypeCombatTick& tick) {
    std::string text;
    for (const auto& note : tick.unresolved) text += " unresolved:" + note;
    for (const auto& range : tick.out_of_range)
        text += " out_of_range:" + std::to_string(range.first) + "->" +
                std::to_string(range.second);
    return text;
}

// One plane pass: fly out, fire exactly one ammo point through the combat
// runtime, return to the pad, dock, and reload.
void plane_pass(PrototypeAircraft& flight, PrototypeCombatRuntime& combat, sw::Snapshot& world,
                std::uint32_t plane, std::uint32_t target, std::uint64_t& frame,
                std::size_t& total_shots, std::size_t& total_damage_events,
                const std::string& name) {
    require(flight.issue(world, plane, target).status == PrototypeAircraftStatus::Complete,
            name + " attack order failed");
    require(combat.attack(world, plane, target), name + " combat order rejected");

    // No shot may leave the gun before the Attack phase arms the plane.
    for (std::uint64_t guard = 0; guard < 4000; ++guard) {
        const auto step = flight.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete,
                name + " flight frame failed: " + step.detail);
        require(step.shots.empty(), name + " flight dealt direct damage");
        const auto tick = combat.tick(world, frame);
        require(count_events(tick, PrototypeCombatEventKind::Shot, plane, true) == 0,
                name + " fired before its Attack phase");
        ++frame;
        if (flight.phase(plane) == AircraftPhase::Attack &&
            flight.ready_to_fire(plane)) break;
    }

    // Exactly one ammo point leaves the magazine through the combat runtime.
    std::size_t shots = 0;
    for (std::uint64_t guard = 0; guard < 2000; ++guard) {
        const auto step = flight.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete,
                name + " flight frame failed: " + step.detail);
        require(step.shots.empty(), name + " flight dealt direct damage");
        const auto tick = combat.tick(world, frame);
        const auto fired = count_events(tick, PrototypeCombatEventKind::Shot, plane, true);
        for (std::size_t i = 0; i < fired; ++i) flight.fired(plane);
        shots += fired;
        total_shots += fired;
        total_damage_events += count_events(tick, PrototypeCombatEventKind::Damage, target, false);
        ++frame;
        if (shots >= 1) break;
        if (guard == 1999) throw std::runtime_error(name +
            " did not fire one shot in range" + diagnostics(combat.tick(world, frame)));
    }
    require(shots == 1, name + " did not fire exactly one shot");
    require(flight.ammo(plane) && *flight.ammo(plane) == 0,
            name + " did not spend exactly one ammo point");

    // Fly home, dock, and reload on the owned pad.
    for (std::uint64_t guard = 0; guard < 8000; ++guard) {
        const auto step = flight.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete,
                name + " return frame failed: " + step.detail);
        ++frame;
        if (flight.phase(plane) == AircraftPhase::Docked &&
            flight.ammo(plane) && *flight.ammo(plane) >= 1) return;
    }
    throw std::runtime_error(name + " never docked and reloaded");
}

// One hover case: fire from cruise height, never land, never dock.
void hover_case(PrototypeAircraft& flight, PrototypeCombatRuntime& combat, sw::Snapshot& world,
                std::uint32_t id, std::uint32_t target, int cruise_z, std::uint64_t& frame,
                std::size_t& total_shots, std::size_t& total_damage_events,
                const std::string& name) {
    require(flight.issue(world, id, target).status == PrototypeAircraftStatus::Complete,
            name + " attack order failed");
    require(combat.attack(world, id, target), name + " combat order rejected");
    std::size_t shots = 0;
    bool landed_low = false, docked = false;
    for (std::uint64_t guard = 0; guard < 4000 && shots == 0; ++guard) {
        const auto step = flight.tick(world, frame);
        require(step.status == PrototypeAircraftStatus::Complete,
                name + " flight frame failed: " + step.detail);
        require(step.shots.empty(), name + " flight dealt direct damage");
        const auto tick = combat.tick(world, frame);
        const auto fired = count_events(tick, PrototypeCombatEventKind::Shot, id, true);
        for (std::size_t i = 0; i < fired; ++i) flight.fired(id);
        shots += fired;
        total_shots += fired;
        total_damage_events += count_events(tick, PrototypeCombatEventKind::Damage, target, false);
        const auto& self = actor(world, id);
        if (self.position && self.position->z < cruise_z) landed_low = true;
        if (flight.phase(id) == AircraftPhase::Docked) docked = true;
        ++frame;
    }
    require(shots >= 1, name + " never fired from cruise height" +
        diagnostics(combat.tick(world, frame)));
    require(!landed_low, name + " dropped below cruise height");
    require(!docked, name + " docked instead of holding cruise height");
}
} // namespace

int main(int argc, char** argv) try {
    const std::string game_dir = argc > 1 ? argv[1] : "game-full";
    fs::MixFileSystem files(game_dir);
    files.bootstrap();
    const auto load = [&](const char* name) {
        const auto bytes = files.read_file(name);
        if (!bytes) throw std::runtime_error(std::string("missing ") + name);
        rules::IniFile ini;
        ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        return ini;
    };
    const auto rules = load("RULES.INI");
    const auto art = load("ART.INI");

    auto world = fixture();
    PrototypeAircraft flight(rules, true);
    PrototypeCombatRuntime combat(rules, {}, art, game_dir + "/game.exe");
    // The fire gate: a flying type only pulls the trigger while its flight
    // mission is in the Attack phase with ammo left.
    combat.set_fire_gate([&](const sw::Entity& e) {
        return (e.id != 11 && e.id != 12 && e.id != 14 && e.id != 15) ||
            flight.ready_to_fire(e.id);
    });

    std::size_t total_shots = 0, total_damage_events = 0;
    std::uint64_t frame = 0;
    const int hp_before = actor(world, 13).resolved_hp ? *actor(world, 13).resolved_hp : 0;
    std::vector<std::string> failures;
    const auto run_case = [&](const char* name, const auto& body) {
        try { body(); std::cout << name << ": cycle complete\n"; }
        catch (const std::exception& error) {
            failures.emplace_back(std::string(name) + ": " + error.what());
            std::cout << name << ": FAILED: " << error.what() << "\n";
        }
    };

    run_case("ORCA", [&] {
        plane_pass(flight, combat, world, 11, 13, frame, total_shots, total_damage_events, "ORCA");
        plane_pass(flight, combat, world, 11, 13, frame, total_shots, total_damage_events, "ORCA");
    });
    run_case("BEAG", [&] {
        plane_pass(flight, combat, world, 12, 13, frame, total_shots, total_damage_events, "BEAG");
        plane_pass(flight, combat, world, 12, 13, frame, total_shots, total_damage_events, "BEAG");
    });
    run_case("ZEP", [&] {
        hover_case(flight, combat, world, 14, 13, 750, frame, total_shots,
                   total_damage_events, "ZEP");
    });
    run_case("JUMPJET", [&] {
        hover_case(flight, combat, world, 15, 13, 500, frame, total_shots,
                   total_damage_events, "JUMPJET");
    });

    const auto& tank = actor(world, 13);
    if (!failures.empty()) {
        require(tank.resolved_hp && *tank.resolved_hp < hp_before,
                "combat events did not damage the target");
        for (const auto& failure : failures) std::cerr << failure << "\n";
        return 1;
    }
    require(total_shots >= 6, "the four aircraft did not complete their firing cycles");
    require(tank.resolved_hp && *tank.resolved_hp < hp_before,
            "combat events did not damage the target");
    std::cout << "HTNK: lost " << (hp_before - *tank.resolved_hp)
              << " HP through " << total_damage_events << " combat events\n";
    std::cout << "GAAIRC: pad held both plane cycles\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "aircraft combat test failed: " << error.what() << "\n";
    return 1;
}
