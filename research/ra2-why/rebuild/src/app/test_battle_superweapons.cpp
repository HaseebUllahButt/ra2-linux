// Part 2 app wiring checks: sidebar entries, the Super Weapons option gate,
// the click panel layout and the EVA speech names. Fails on code without
// battle_superweapons.h.
#include "battle_superweapons.h"
#include <iostream>
#include <stdexcept>

namespace {
namespace w = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::SuperKind;
using ra2::sim::PrototypeSuperweapons;

void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }

IniFile fixture_rules() {
    IniFile rules;
    rules.parse(
        "[SuperWeaponTypes]\n1=MultiSpecial\n2=IronCurtainSpecial\n3=SpyPlaneSpecial\n"
        "[MultiSpecial]\nName=N U K E !!!\nIsPowered=true\nRechargeTime=10\nType=MultiMissile\n"
        "[IronCurtainSpecial]\nIsPowered=true\nRechargeTime=5\nType=IronCurtain\n"
        "[SpyPlaneSpecial]\nIsPowered=false\nRechargeTime=4\nType=SpyPlane\n"
        "[BuildingTypes]\n1=NAMISL\n2=NAIRON\n3=AMRADR\n"
        "[NAMISL]\nSuperWeapon=MultiSpecial\nDisableableFromShell=yes\n"
        "[NAIRON]\nSuperWeapon=IronCurtainSpecial\nDisableableFromShell=yes\n"
        "[AMRADR]\nSuperWeapon=SpyPlaneSpecial\n");
    return rules;
}

w::Snapshot world_with_silo() {
    w::Snapshot world;
    world.edition = w::Edition::Base;
    world.setup_progress = w::SetupProgress::PrototypeComplete;
    w::House house;
    house.id = 101;house.country = "Russians";
    house.power_output = 200;house.power_drain = 100;
    world.houses = {house};
    for (int y = 5; y <= 80; ++y)
        for (int x = 5; x <= 80; ++x) {
            w::CellState cell;
            cell.allocated = true;cell.heads.emplace();cell.cell_flag_100 = false;
            cell.ground_occupancy_124 = 0;cell.terrain.emplace();
            cell.terrain->cell = {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            cell.terrain->floor_world_height = 0;cell.terrain->ramp_index = 0;
            cell.terrain->overlay_type_index = -1;
            world.cells.emplace(static_cast<std::uint32_t>(y) * 512 + x, cell);
        }
    w::Entity silo;
    silo.id = 1;silo.house_id = 101;silo.kind = w::Kind::Structure;silo.type = "NAMISL";
    silo.lifetime = w::Lifetime::Placed;silo.resolved_hp = 100;
    silo.cell = ra2map::Cell{10, 10};silo.position = ra2::sim::Coord{2560, 2560, 0};
    world.entities.push_back(silo);
    w::Entity radar = silo;
    radar.id = 2;radar.type = "AMRADR";
    world.entities.push_back(radar);
    return world;
}


}

int main() try {
    const auto rules = fixture_rules();
    const auto types = ra2::sim::read_superweapon_types(rules);
    require(types.size() == 3, "three super weapon types expected");

    // Speech names follow the EVA.INI sections of the original game.
    require(ra2::app::superweapon_ready_speech(SuperKind::Nuke) == "EVA_NuclearMissileReady",
        "nuke ready speech");
    require(ra2::app::superweapon_ready_speech(SuperKind::LightningStorm) ==
        "EVA_WeatherControlDeviceReady", "storm ready speech");
    require(ra2::app::superweapon_launch_speech(SuperKind::Nuke) ==
        "EVA_NuclearMissileLaunchDetected", "nuke launch speech");
    require(ra2::app::superweapon_launch_speech(SuperKind::IronCurtain).empty(),
        "iron curtain has no launch warning");

    // The Super Weapons option gate: every granting building must have
    // DisableableFromShell=yes (House::Can_Build 004E3832).
    const auto disableable = [&](const std::string& building) {
        return rules.read_bool(building, "DisableableFromShell");
    };
    const auto blocked = ra2::app::superweapons_blocked_by_option(rules, types, disableable);
    require(blocked.count("MultiSpecial") == 1, "nuke silo weapon is switchable");
    require(blocked.count("IronCurtainSpecial") == 1, "iron curtain weapon is switchable");
    require(blocked.count("SpyPlaneSpecial") == 0, "spy plane weapon stays");

    // Sidebar entries: granted weapons only, with the charge clock.
    PrototypeSuperweapons superweapons(rules);
    auto world = world_with_silo();
    superweapons.update_grants(world);
    std::string store;
    const auto entries = ra2::app::superweapon_entries(types, superweapons, 101, blocked,
        [&](const std::string& type) -> std::string { store = type; return type; });
    require(entries.size() == 1, "only the spy plane weapon is not blocked");
    require(entries.front().section == "SpyPlaneSpecial", "spy plane entry first");
    require(entries.front().frames_left == 4 * 900, "RechargeTime 4 minutes starts the clock");
    require(!entries.front().ready, "a fresh weapon is not ready");
    superweapons.tick(world, 1);
    superweapons.tick(world, 2);
    const auto later = ra2::app::superweapon_entries(types, superweapons, 101, blocked,
        [&](const std::string&) { return std::string(); });
    require(later.front().frames_left == 4 * 900 - 2, "the clock counts down per frame");

    // Fire plan: the ChronoSphere fires at the source first, then the
    // PreDependent ChronoWarp at the destination (base 006be1c0 order).
    const auto chrono_rules = [&]{
        IniFile rules;
        rules.parse(
            "[SuperWeaponTypes]\n1=ChronoSphereSpecial\n2=ChronoWarpSpecial\n"
            "[ChronoSphereSpecial]\nType=ChronoSphere\n"
            "[ChronoWarpSpecial]\nType=ChronoWarp\n"
            "[BuildingTypes]\n1=GACSPH\n[GACSPH]\nSuperWeapon=ChronoSphereSpecial\n");
        return rules;
    }();
    const auto chrono_types = ra2::sim::read_superweapon_types(chrono_rules);
    const ra2map::Cell source{5, 6}, destination{9, 9};
    const auto plan = ra2::app::superweapon_fire_plan(chrono_types,
        "ChronoSphereSpecial", source, destination);
    require(plan.size() == 2, "chrono plan has two fire calls");
    require(plan[0].section == "ChronoSphereSpecial" &&
        plan[0].cell.x == source.x && plan[0].cell.y == source.y,
        "the sphere fires at the source first");
    require(plan[1].section == "ChronoWarpSpecial" &&
        plan[1].cell.x == destination.x && plan[1].cell.y == destination.y,
        "the warp fires at the destination second");
    require(ra2::app::superweapon_fire_plan(chrono_types, "ChronoSphereSpecial",
        std::nullopt, destination).empty(), "a chrono pick without a source has no plan");
    const auto single = ra2::app::superweapon_fire_plan(chrono_types,
        "ChronoWarpSpecial", std::nullopt, destination);
    require(single.size() == 1 && single[0].cell.x == destination.x,
        "a plain weapon fires once at the target");

    // Functional regression: the click order moves the source tank to the
    // second click. The sphere fires at the tank's cell, the warp fires at
    // the destination, and the tank lands there.
    auto chrono_world = world_with_silo();
    chrono_world.entities.clear();
    w::Entity yard;
    yard.id = 1;yard.house_id = 101;yard.kind = w::Kind::Structure;yard.type = "GACSPH";
    yard.lifetime = w::Lifetime::Placed;yard.resolved_hp = 1000;
    yard.cell = ra2map::Cell{10, 10};yard.position = ra2::sim::Coord{2560, 2560, 0};
    chrono_world.entities.push_back(yard);
    w::Entity tank = yard;
    tank.id = 2;tank.kind = w::Kind::Unit;tank.type = "HTNK";
    tank.cell = ra2map::Cell{50, 50};tank.position = ra2::sim::Coord{50 * 256 + 128, 50 * 256 + 128, 0};
    tank.layer = w::Layer::Ground;
    tank.next = 0; // Chain end, as the sim fixture's first cell occupant.
    chrono_world.entities.push_back(tank);
    for (auto& pair : chrono_world.cells) {
        pair.second.heads.emplace();
        pair.second.ground_occupancy_124 = 0;
        if (pair.second.terrain)pair.second.terrain->floor_world_height = 0;
    }
    chrono_world.cells.at(50u * 512 + 50).heads->ground_head = 2;
    PrototypeSuperweapons supers(chrono_rules);
    supers.update_grants(chrono_world);
    for (int frame = 0; frame < 6300; ++frame)
        supers.tick(chrono_world, static_cast<std::uint64_t>(frame));
    require(supers.ready(101, "ChronoSphereSpecial"), "the sphere did not charge");
    const ra2map::Cell tank_cell{50, 50}, landing{55, 55};
    const auto chrono_plan = ra2::app::superweapon_fire_plan(chrono_types,
        "ChronoSphereSpecial", tank_cell, landing);
    require(chrono_plan.size() == 2, "the chrono plan is not a pair");
    const auto arm = supers.fire(chrono_world, 101, chrono_plan[0].section, chrono_plan[0].cell, 6300);
    require(arm.fired, "the sphere click was rejected");
    const auto warp = supers.fire(chrono_world, 101, chrono_plan[1].section, chrono_plan[1].cell, 6301);
    require(warp.fired && warp.teleported == 1, "the warp click did not move the tank");
    require(chrono_world.entities[1].cell && chrono_world.entities[1].cell->x == landing.x &&
        chrono_world.entities[1].cell->y == landing.y,
        "the source tank did not arrive at the second click");

    // Panel layout: rows are hit only inside the panel, on the left edge as
    // the original places the special weapon buttons.
    using ra2::app::SuperweaponPanel;
    require(SuperweaponPanel::x == 0, "the panel sits on the left screen edge");
    require(!SuperweaponPanel::row_at(SuperweaponPanel::x - 1, SuperweaponPanel::y, 1),
        "left of the panel misses");
    require(SuperweaponPanel::row_at(SuperweaponPanel::x + 5, SuperweaponPanel::y + 5, 1) == 0u,
        "the first row hits");
    require(!SuperweaponPanel::row_at(SuperweaponPanel::x + 5,
        SuperweaponPanel::y + SuperweaponPanel::row_height, 1), "past the last row misses");

    std::cout << "battle_superweapons checks passed: speech names, option gate, entries, panel\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "test_battle_superweapons: " << error.what() << '\n';
    return 1;
}
