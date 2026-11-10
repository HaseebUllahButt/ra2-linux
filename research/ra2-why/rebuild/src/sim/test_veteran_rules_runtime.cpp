// Runtime wiring test for PrototypeCombatRuntime.
// Checks that [General] VeteranCombat, the FIREPOWER veteran ability and
// the map override reach real tick() damage events.
#include <rules/ini_file.h>
#include <sim/damage_runtime.h>
#include <sim/prototype_special.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ra2;
namespace world = ra2::session::world;

namespace {

int failures = 0;

void check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; ++failures; }
}

world::Snapshot make_world() {
    world::Snapshot w;
    w.random = sim::Random(0).state();
    for (int y = 46; y <= 54; ++y) {
        for (int x = 46; x <= 56; ++x) {
            world::CellState c;
            c.allocated = true;
            c.heads.emplace();
            c.cell_flag_100 = false;
            c.ground_occupancy_124 = 0;
            c.terrain.emplace();
            c.terrain->cell = {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            c.terrain->floor_world_height = 0;
            c.terrain->ramp_index = 0;
            c.terrain->overlay_type_index = -1;
            w.cells.emplace(y * 512 + x, c);
        }
    }
    world::House a;
    a.id = 101;
    a.reload_multiplier = 1;
    a.firepower_multiplier = 1;
    world::House b = a;
    b.id = 102;
    b.human_controlled = true;
    w.houses = {a, b};
    world::Entity source;
    source.id = 1;
    source.house_id = 101;
    source.type = "SRC";
    source.kind = world::Kind::Infantry;
    source.lifetime = world::Lifetime::Placed;
    source.position = sim::Coord{50 * 256 + 128, 50 * 256 + 128, 0};
    source.cell = ra2map::Cell{50, 50};
    source.next = 0;
    source.resolved_hp = source.maximum_hp = 200;
    source.active = true;
    source.in_limbo = false;
    source.hull_facing_raw = 0;
    source.turret_facing_raw = 0;
    source.weapon_timer_start_frame = -1;
    source.weapon_timer_duration = 0;
    source.burst_index = 0;
    source.armor_multiplier = 1;
    source.house_armor_multiplier = 1;
    source.firepower_multiplier = 1;
    source.collision_eligible = true;
    source.collision_virtual_position = source.position;
    source.veterancy = 1.0;
    source.mission = 13;
    auto target = source;
    target.id = 2;
    target.house_id = 102;
    target.type = "TGT";
    target.position = sim::Coord{54 * 256 + 128, 50 * 256 + 128, 0};
    target.cell = ra2map::Cell{54, 50};
    target.resolved_hp = target.maximum_hp = 1000;
    target.veterancy = 0.0;
    target.collision_virtual_position = target.position;
    w.entities = {source, target};
    return w;
}

std::string game_exe;

const char* base_rules =
    "[General]\n"
    "VeteranCombat=1.1\n"
    "VeteranArmor=1.5\n"
    "[CombatDamage]\n"
    "MaxDamage=10000\n"
    "[WEAPON]\n"
    "Damage=100\n"
    "ROF=60\n"
    "Range=10\n"
    "Warhead=WH\n"
    "Projectile=PROJ\n"
    "[WH]\n"
    "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
    "[SRC]\n"
    "Primary=WEAPON\n"
    "Armor=heavy\n"
    "VeteranAbilities=FIREPOWER\n"
    "[TGT]\n"
    "Strength=1000\n"
    "Armor=heavy\n"
    "BuildCat=Combat\n"
    "[TGTB]\n"
    "Strength=1000\n"
    "Armor=heavy\n"
    "[AttackerCountry]\n"
    "[TargetCountry]\n"
    "ArmorInfantryMult=0.5\n"
    "ArmorUnitsMult=0.8\n"
    "ArmorAircraftMult=1.5\n"
    "ArmorBuildingsMult=1.2\n"
    "ArmorDefensesMult=0.9\n";

rules::IniFile parse(const char* text) {
    rules::IniFile ini;
    ini.parse(std::string_view(text));
    return ini;
}

rules::IniFile parse(const std::string& text) {
    rules::IniFile ini;
    ini.parse(text);
    return ini;
}

int damage_after_tick(const rules::IniFile& base, const rules::IniFile& map) {
    auto w = make_world();
    const rules::IniFile art = parse("");
    sim::PrototypeCombatRuntime runtime(base, map, art, game_exe);
    check(runtime.attack(w, 1, 2), "runtime rejected the attack order");
    const auto report = runtime.tick(w, 0);
    check(report.fired == 1, "no shot fired");
    check(report.unresolved.empty(), "tick reported unresolved state");
    int damage = 0, hp_before = 0, hp_after = 0;
    for (const auto& event : report.events) {
        if (event.kind == sim::PrototypeCombatEventKind::Damage) {
            damage = event.raw_damage;
            hp_before = event.hp_before;
            hp_after = event.hp_after;
        }
    }
    // Target armor must show in the real health change, not just the
    // projectile multiplier.
    check(hp_before - hp_after == damage, "target health did not drop by the event damage");
    return damage;
}

// A world whose houses carry countries, with the target kind and type
// varied per case.
world::Snapshot make_country_world(world::Kind target_kind, const char* target_type) {
    auto w = make_world();
    w.houses[0].country = "AttackerCountry";
    w.houses[1].country = "TargetCountry";
    auto& target = w.entities[1];
    target.kind = target_kind;
    target.type = target_type;
    return w;
}

// Runs one combat tick and returns the target health loss.
int country_damage_after_tick(const rules::IniFile& base, const rules::IniFile& map,
                              world::Kind target_kind, const char* target_type) {
    auto w = make_country_world(target_kind, target_type);
    const rules::IniFile art = parse("");
    sim::PrototypeCombatRuntime runtime(base, map, art, game_exe);
    check(runtime.attack(w, 1, 2), "runtime rejected the country attack order");
    const auto report = runtime.tick(w, 0);
    check(report.fired == 1, "no country shot fired");
    check(report.unresolved.empty(), "country tick reported unresolved state");
    int loss = 0;
    for (const auto& event : report.events) {
        if (event.kind == sim::PrototypeCombatEventKind::Damage) {
            loss = event.hp_before - event.hp_after;
        }
    }
    check(loss > 0, "country target health did not drop");
    return loss;
}

// One tick against a target with a fixed veterancy and one ability line.
// Used to test the exact FUN007133A0 / 00713420 rank boundaries.
int rank_damage_after_tick(double veterancy, const char* ability_line) {
    auto w = make_world();
    w.entities[1].veterancy = veterancy;
    const auto rank_map = parse((std::string("[TGT]\nStrength=1000\nArmor=heavy\n")
        + ability_line + "\n").c_str());
    const rules::IniFile art = parse("");
    sim::PrototypeCombatRuntime runtime(parse(base_rules), rank_map, art, game_exe);
    check(runtime.attack(w, 1, 2), "runtime rejected the rank attack order");
    const auto report = runtime.tick(w, 0);
    check(report.fired == 1, "no rank shot fired");
    check(report.unresolved.empty(), "rank tick reported unresolved state");
    int loss = 0;
    for (const auto& event : report.events) {
        if (event.kind == sim::PrototypeCombatEventKind::Damage) {
            loss = event.hp_before - event.hp_after;
        }
    }
    check(loss > 0, "rank target health did not drop");
    return loss;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) game_exe = std::string(argv[1]) + "/game.exe";
    const auto base = parse(base_rules);
    const rules::IniFile empty = parse("");
    const int base_damage = damage_after_tick(base, empty);
    check(base_damage == 110, "base VeteranCombat did not reach the damage event");
    const auto map = parse("[General]\nVeteranCombat=1.5\n");
    const int map_damage
 = damage_after_tick(base, map);
    check(map_damage == 150, "map VeteranCombat override did not reach the damage event");

    // A promoted target takes less damage when the map rules override
    // VeteranArmor and give the target the STRONGER ability.
    {
        auto w = make_world();
        w.entities[1].veterancy = 1.0;
        const auto promoted_map = parse(
            "[General]\nVeteranArmor=2.0\n"
            "[TGT]\nStrength=1000\nArmor=heavy\n"
            "VeteranAbilities=STRONGER\n");
        const rules::IniFile art = parse("");
        sim::PrototypeCombatRuntime runtime(base, promoted_map, art, game_exe);
        check(runtime.attack(w, 1, 2), "runtime rejected the promoted attack");
        const auto report = runtime.tick(w, 0);
        check(report.fired == 1, "no promoted shot fired");
        check(report.unresolved.empty(), "promoted tick reported unresolved state");
        int after = 0;
        for (const auto& event : report.events) {
            if (event.kind == sim::PrototypeCombatEventKind::Damage) {
                after = event.hp_after;
            }
        }
        check(after == 945, "promoted target armor override did not reach the damage event");
    }

    // Base 004F58A0: the owning House.country picks an armor factor by
    // target RTTI. The shot is 110 (VeteranCombat 1.1), so each factor
    // divides the health loss directly.
    check(country_damage_after_tick(base, empty, world::Kind::Infantry, "TGT") == 220,
          "country ArmorInfantryMult did not reach the damage event");
    check(country_damage_after_tick(base, empty, world::Kind::Unit, "TGT") == 137,
          "country ArmorUnitsMult did not reach the damage event");
    check(country_damage_after_tick(base, empty, world::Kind::Structure, "TGT") == 122,
          "country ArmorDefensesMult did not reach the combat building");
    check(country_damage_after_tick(base, empty, world::Kind::Structure, "TGTB") == 91,
          "country ArmorBuildingsMult did not reach the plain building");

    // The map file overrides the country factor.
    const auto country_map = parse("[TargetCountry]\nArmorInfantryMult=2.0\n");
    check(country_damage_after_tick(base, country_map, world::Kind::Infantry, "TGT") == 55,
          "map country armor override did not reach the damage event");

    // Aircraft targets have no combat-tick path yet (prototype_alive keeps
    // Aircraft out), so the 004F58A0 branch is checked on the getter.
    {
        const auto w = make_country_world(world::Kind::Aircraft, "TGT");
        check(sim::country_armor_multiplier(&w.houses[1], world::Kind::Aircraft, "TGT",
                  base, empty) == 1.5,
              "country ArmorAircraftMult getter is wrong");
        check(sim::country_armor_multiplier(&w.houses[1], world::Kind::Terrain, "TGT",
                  base, empty) == 1.0,
              "unknown RTTI must keep the 1.0 country factor");
        check(sim::country_armor_multiplier(nullptr, world::Kind::Infantry, "TGT",
                  base, empty) == 1.0,
              "a missing house must keep the 1.0 country factor");
    }

    // Crate armor (+150% shipped 0064D490 factor), veteran rank and the
    // country factor compose in the real tick: 110/(0.5*1.5)=146 then
    // 146/1.5 (VeteranArmor) = 97.
    {
        const auto crate_base = parse(std::string(base_rules) +
            "[Powerups]\nArmor=10,ARMOR,yes,150%\n");
        const auto rank_map = parse("[TGT]\nVeteranAbilities=STRONGER\n");
        auto w = make_country_world(world::Kind::Infantry, "TGT");
        const auto crate_rules = sim::CrateRules::load(crate_base);
        sim::CrateState crates;
        check(crates.place(50 * 512 + 54, sim::CrateKind::Armor, 0, crate_rules),
              "armor crate placement failed");
        const auto applied = crates.collect(crate_base, w, crate_rules, 2, 50 * 512 + 54);
        check(applied.kind == sim::CrateKind::Armor, "wrong crate kind");
        check(w.entities[1].armor_multiplier.value_or(1.0) == 1.5,
              "armor crate factor did not reach the target");
        w.entities[1].veterancy = 1.0;
        const rules::IniFile art = parse("");
        sim::PrototypeCombatRuntime runtime(crate_base, rank_map, art, game_exe);
        check(runtime.attack(w, 1, 2), "runtime rejected the composed attack order");
        const auto report = runtime.tick(w, 0);
        check(report.fired == 1, "no composed shot fired");
        check(report.unresolved.empty(), "composed tick reported unresolved state");
        int loss = 0;
        for (const auto& event : report.events) {
            if (event.kind == sim::PrototypeCombatEventKind::Damage) {
                loss = event.hp_before - event.hp_after;
            }
        }
        check(loss == 97, "crate, rank and country factors did not compose");
    }

    // 004F9550 stores each factor as binary32; the store uses control word
    // 00000E7F (toward zero). The INI reader 0x5283D0 already narrows to a
    // float, so the getter must return that binary32 value, never a wider
    // double, and a toward-zero store must never round it up.
    {
        const auto w = make_country_world(world::Kind::Unit, "TGT");
        const double factor = sim::country_armor_multiplier(&w.houses[1],
            world::Kind::Unit, "TGT", base, empty);
        const double reader = rules::IniFile::double_value("0.8").value_or(0.0);
        float stored = static_cast<float>(reader);
        if (static_cast<double>(stored) > reader) stored = std::nextafter(stored, 0.0f);
        check(factor == static_cast<double>(stored),
              "country factor is not the toward-zero binary32 store");
        check(factor != 0.8, "country factor kept the full INI double");
    }

    // BuildCat is case-insensitive: a map file may write lowercase combat.
    const auto lowercase_map = parse("[TGTB]\nBuildCat=combat\n");
    check(country_damage_after_tick(base, lowercase_map, world::Kind::Structure, "TGTB") == 122,
          "lowercase map BuildCat=combat did not pick the defense factor");

    // Base FUN007133A0: exactly 1 <= value < 2 is Veteran. Base 00713420:
    // exactly 2 <= value is Elite. No epsilon around the thresholds.
    const double below_veteran = std::nextafter(1.0, 0.0);
    const double below_elite = std::nextafter(2.0, 0.0);
    check(rank_damage_after_tick(below_veteran, "VeteranAbilities=STRONGER") == 110,
          "veteran threshold gained an epsilon below 1");
    check(rank_damage_after_tick(1.0, "VeteranAbilities=STRONGER") == 73,
          "exactly 1 is not Veteran");
    check(rank_damage_after_tick(below_elite, "EliteAbilities=STRONGER") == 110,
          "elite threshold gained an epsilon below 2");
    check(rank_damage_after_tick(2.0, "EliteAbilities=STRONGER") == 73,
          "exactly 2 is not Elite");
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "veteran runtime damage checks passed\n";
    return 0;
}
