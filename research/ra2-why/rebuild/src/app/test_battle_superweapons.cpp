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
    const auto chrono_types = [&]{
        IniFile chrono_rules;
        chrono_rules.parse(
            "[SuperWeaponTypes]\n1=ChronoSphereSpecial\n2=ChronoWarpSpecial\n"
            "[ChronoSphereSpecial]\nType=ChronoSphere\n"
            "[ChronoWarpSpecial]\nType=ChronoWarp\n");
        return ra2::sim::read_superweapon_types(chrono_rules);
    }();
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
