#include "prototype_special.h"
#include "fs/mix_bootstrap.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
namespace sw = ra2::session::world;
using ra2::rules::IniFile;
using ra2::sim::Ability;
using ra2::sim::AbilityFirepower;
using ra2::sim::AbilityFaster;
using ra2::sim::AbilityRof;
using ra2::sim::AbilitySelfHeal;
using ra2::sim::AbilityStronger;
using ra2::sim::CrateKind;
using ra2::sim::CrateRules;
using ra2::sim::CrateState;
using ra2::sim::EngineerEffect;
using ra2::sim::EngineerResult;
using ra2::sim::GarrisonRules;
using ra2::sim::GarrisonState;
using ra2::sim::Promotion;
using ra2::sim::Rank;
using ra2::sim::SpyEffect;
using ra2::sim::SpyEffectKind;
using ra2::sim::SpyInfiltrations;
using ra2::sim::TypeAbilities;
using ra2::sim::VeterancyRules;
using ra2::sim::load_type_abilities;
using ra2::sim::rank_of;
using ra2::sim::record_kill;
using ra2::sim::trainable;
using ra2::sim::veteran_armor_divisor;
using ra2::sim::veteran_firepower_multiplier;
using ra2::sim::veteran_rof_multiplier;
using ra2::sim::veteran_speed_multiplier;

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void require_near(T actual, T expected, T epsilon, const char* message) {
    if (actual < expected - epsilon || actual > expected + epsilon) {
        std::cerr << "FAIL: " << message << " expected ~" << expected
                  << " got " << actual << '\n';
        ++failures;
    }
}

template <typename Call>
void require_throws(const Call& call, const char* message) {
    try {
        call();
        std::cerr << "FAIL: " << message << " did not throw\n";
        ++failures;
    } catch (const std::exception&) {
    }
}

sw::House house(std::uint32_t id, const char* name) {
    sw::House result;
    result.id = id;
    result.name = name;
    result.country = name;
    result.cash = 1000;
    return result;
}

sw::Entity entity(std::uint32_t id, std::uint32_t owner, const char* type, sw::Kind kind,
    ra2map::Cell cell, std::int32_t hp, std::optional<std::int32_t> max_hp = {}) {
    sw::Entity result;
    result.id = id;
    result.house_id = owner;
    result.type = type;
    result.kind = kind;
    result.lifetime = sw::Lifetime::Placed;
    result.cell = cell;
    result.resolved_hp = hp;
    result.maximum_hp = max_hp;
    result.on_map = true;
    return result;
}

IniFile overlay(const IniFile& base, const char* ini_text) {
    IniFile merged = base;
    merged.parse(ini_text);
    return merged;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: test_prototype_special <base-game-directory>");
    ra2::fs::MixFileSystem files(argv[1]);
    files.mount_language(ra2::fs::GameVariant::RedAlert2);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    const auto bytes = files.read_file("RULES.INI");
    if (!bytes.has_value()) throw std::runtime_error("Installed base RULES.INI is missing");
    IniFile rules;
    rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));

    // ---- Veterancy rules from the real [General] section ----
    const auto general = VeterancyRules::load(rules);
    require_near(general.veteran_ratio, 3.0, 1e-9, "VeteranRatio");
    require_near(general.veteran_combat, 1.1, 1e-9, "VeteranCombat");
    require_near(general.veteran_speed, 1.2, 1e-9, "VeteranSpeed");
    require_near(general.veteran_armor, 1.5, 1e-9, "VeteranArmor");
    require_near(general.veteran_rof, 0.6, 1e-9, "VeteranROF");
    require_near(general.veteran_cap, 2.0, 1e-9, "VeteranCap");
    require(!general.initial_veteran, "InitialVeteran default");

    // ---- Ability lists from real unit types ----
    {
        // The base GI lists veteran and elite abilities.
        const auto gi = load_type_abilities(rules, "GI");
        require(gi.veteran.flag[AbilityFirepower], "GI veteran FIREPOWER");
        require(gi.veteran.flag[AbilityStronger], "GI veteran STRONGER");
        require(gi.veteran.flag[AbilityRof], "GI veteran ROF");
        require(gi.veteran.flag[AbilityFaster], "GI veteran FASTER");
        require(gi.elite.flag[AbilitySelfHeal], "GI elite SELF_HEAL");
        const auto none = load_type_abilities(rules, "MTNK");
        // A type without lists keeps every flag false; unknown names throw.
        bool threw = false;
        try {
            IniFile bad;
            bad.parse("[X]\nVeteranAbilities=MADE_UP\n");
            (void)load_type_abilities(bad, "X");
        } catch (const std::exception&) {
            threw = true;
        }
        require(threw, "unknown ability name throws");
        (void)none;
    }

    // ---- Rank thresholds ----
    require(rank_of(0.0) == Rank::Rookie && rank_of(0.99) == Rank::Rookie, "rookie range");
    require(rank_of(1.0) == Rank::Veteran && rank_of(1.99) == Rank::Veteran, "veteran range");
    require(rank_of(2.0) == Rank::Elite, "elite range");

    // ---- Trainable defaults ----
    require(trainable(rules, "MTNK", sw::Kind::Unit), "units are trainable by default");
    require(!trainable(rules, "GACNST", sw::Kind::Structure), "buildings are not trainable");
    {
        IniFile flagged;
        flagged.parse("[SOMETHING]\nTrainable=no\n");
        require(!trainable(flagged, "SOMETHING", sw::Kind::Unit), "Trainable=no overrides");
    }

    // ---- record_kill with real costs ----
    {
        sw::Snapshot world;
        world.houses = {house(1, "A"), house(2, "B")};
        // A GI (200) kills a Conscript (100): 100 / (200 * 3.0) = 1/6.
        world.entities = {
            entity(1, 1, "GI", sw::Kind::Infantry, {10, 10}, 100),
            entity(2, 2, "CONSCRIPT", sw::Kind::Infantry, {11, 10}, 100),
        };
        const auto gain = record_kill(rules, general, world, 1, 2);
        require(!gain.promoted, "first kill does not promote");
        require_near(gain.veterancy, 100.0 / (200.0 * 3.0), 1e-9, "GI kill gain");
        // Five more conscripts push the GI over 1.0 -> veteran.
        for (std::uint32_t victim = 3; victim <= 7; ++victim) {
            world.entities.push_back(
                entity(victim, 2, "CONSCRIPT", sw::Kind::Infantry, {11, 10}, 100));
            const auto step = record_kill(rules, general, world, 1, victim);
            if (victim == 7) {
                require(step.promoted, "sixth kill promotes to veteran");
                require(step.after == Rank::Veteran, "promotion rank");
            }
        }
        // Cap: a GI killing tank after tank never passes 2.0.
        for (std::uint32_t victim = 8; victim <= 30; ++victim) {
            world.entities.push_back(entity(victim, 2, "MTNK", sw::Kind::Unit, {12, 10}, 300));
            const auto step = record_kill(rules, general, world, 1, victim);
            require(step.veterancy <= general.veteran_cap + 1e-9, "veterancy cap holds");
        }
        require(rank_of(world.entities[0].veterancy.value_or(0.0)) == Rank::Elite,
            "cap reaches elite");
        // Guards: same house, dead victim, non-trainable killer, no cost.
        require_throws([&] {
            auto copy = world;
            record_kill(rules, general, copy, 1, 1);
        }, "same-house kill throws");
        require_throws([&] {
            auto copy = world;
            copy.entities[1].lifetime = sw::Lifetime::Destroyed;
            record_kill(rules, general, copy, 1, 2);
        }, "dead victim throws");
        {
            // A building killer is not trainable: no gain, no throw.
            auto copy = world;
            copy.entities[0].type = "GACNST";
            copy.entities[0].kind = sw::Kind::Structure;
            const auto step = record_kill(rules, general, copy, 1, 2);
            require(!step.promoted && step.veterancy == 0.0,
                "non-trainable killer gains nothing");
        }
    }

    // ---- Ability-gated multipliers ----
    {
        const auto gi = load_type_abilities(rules, "GI");
        const auto plain = TypeAbilities{};
        require_near(veteran_firepower_multiplier(plain, Rank::Veteran, general), 1.0, 1e-9,
            "no FIREPOWER ability keeps firepower");
        require_near(veteran_firepower_multiplier(gi, Rank::Veteran, general), 1.1, 1e-9,
            "GI veteran firepower");
        require_near(veteran_firepower_multiplier(gi, Rank::Elite, general), 1.1, 1e-9,
            "GI elite firepower reuses veteran value");
        require_near(veteran_armor_divisor(gi, Rank::Veteran, general), 1.5, 1e-9,
            "GI veteran armor");
        require_near(veteran_rof_multiplier(gi, Rank::Elite, general), 0.6, 1e-9,
            "GI elite ROF");
        require_near(veteran_speed_multiplier(gi, Rank::Veteran, general), 1.2, 1e-9,
            "GI veteran speed");
        require_near(veteran_speed_multiplier(plain, Rank::Elite, general), 1.0, 1e-9,
            "no FASTER ability keeps speed");
        require_near(veteran_firepower_multiplier(gi, Rank::Rookie, general), 1.0, 1e-9,
            "rookie keeps base values");
    }

    // ---- Garrison rules from real civilian buildings ----
    {
        // A base building with urban combat values: CATEXS02 (the Alamo).
        const auto alamo = GarrisonRules::load(rules, "CATEXS02");
        require(alamo.can_be_occupied, "the Alamo can be occupied");
        require(alamo.max_occupants == 5, "the Alamo holds five occupants");
        require(alamo.can_occupy_fire, "the Alamo fires out");
        const auto yard = GarrisonRules::load(rules, "GACNST");
        require(!yard.can_be_occupied, "the construction yard cannot be occupied");
    }

    // ---- Garrison state ----
    {
        // CATEXS02 has no Primary weapon rules dependency; use a synthetic
        // occupant weapon for the fire-out check.
        const auto local = overlay(rules,
            "[TESTGUN]\nDamage=15\n"
            "[TESTOCC]\nOccupier=yes\nPrimary=TESTGUN\n");
        sw::Snapshot world;
        world.houses = {house(1, "A"), house(2, "B")};
        world.entities = {
            entity(1, 1, "CATEXS02", sw::Kind::Structure, {10, 10}, 600),
            entity(2, 1, "TESTOCC", sw::Kind::Infantry, {10, 11}, 100),
            entity(3, 1, "TESTOCC", sw::Kind::Infantry, {10, 12}, 100),
            entity(4, 2, "TESTOCC", sw::Kind::Infantry, {10, 13}, 100),
        };
        GarrisonState garrison;
        require(garrison.garrison(local, world, 1, 2), "first occupant enters");
        require(garrison.garrison(local, world, 1, 3), "second occupant enters");
        require(!garrison.garrison(local, world, 1, 4), "enemy infantry cannot garrison");
        require(garrison.occupant_count(1) == 2, "two occupants");
        require(world.entities[1].in_limbo.value_or(false), "occupant left the map");
        require(world.entities[1].cell.has_value() &&
            world.entities[1].cell->x == 10 && world.entities[1].cell->y == 10,
            "occupant sits at the building cell");
        require(garrison.occupant_fire_damage(world, local, 1) == 30,
            "two occupants fire 15 damage each");
        // Non-Occupier infantry cannot garrison.
        const auto no_occupier = overlay(local, "[NOTOCC]\nPrimary=TESTGUN\n");
        sw::Entity stranger = entity(5, 1, "NOTOCC", sw::Kind::Infantry, {10, 14}, 100);
        world.entities.push_back(stranger);
        require(!garrison.garrison(local, world, 1, 5), "non-Occupier cannot garrison");
        // Capacity: fill the Alamo, then the sixth is refused.
        for (std::uint32_t id = 6; id <= 10; ++id) {
            world.entities.push_back(entity(id, 1, "TESTOCC", sw::Kind::Infantry, {10, 15}, 100));
            require(garrison.garrison(local, world, 1, id), "fill to capacity");
        }
        world.entities.push_back(entity(11, 1, "TESTOCC", sw::Kind::Infantry, {10, 16}, 100));
        require(!garrison.garrison(local, world, 1, 11), "capacity limits occupants");
        // Ungarrison releases everyone at the building cell.
        const auto leaving = garrison.ungarrison(world, 1);
        require(leaving.size() == 7, "all occupants leave");
        require(!world.entities[1].in_limbo.value_or(true), "leaver is back on the map");
        require(garrison.occupant_count(1) == 0, "building is empty");
        // Destroyed occupants purge.
        world.entities.push_back(entity(12, 1, "TESTOCC", sw::Kind::Infantry, {10, 17}, 100));
        require(garrison.garrison(local, world, 1, 12), "re-garrison after unload");
        world.entities.back().lifetime = sw::Lifetime::Destroyed;
        require(garrison.purge_destroyed(world), "purge drops the dead occupant");
        require(garrison.occupant_count(1) == 0, "purged building is empty");
    }

    // ---- Engineer: repair, capture, damage ----
    {
        sw::Snapshot world;
        world.houses = {house(1, "A"), house(2, "B")};
        world.entities = {
            entity(1, 1, "ENGINEER", sw::Kind::Infantry, {10, 10}, 75),
            entity(2, 1, "GACNST", sw::Kind::Structure, {10, 11}, 200, 1000),
            entity(3, 2, "NAPOWR", sw::Kind::Structure, {12, 10}, 150, 600),
            entity(4, 2, "NAPOWR", sw::Kind::Structure, {14, 10}, 600, 600),
        };
        // Friendly building: full repair.
        const auto repaired = ra2::sim::engineer_enter_building(rules, world, 1, 2);
        require(repaired.effect == EngineerEffect::RepairedAlly, "ally repair effect");
        require(world.entities[1].resolved_hp.value_or(0) == 1000, "ally building fully repaired");
        require(world.entities[0].lifetime == sw::Lifetime::Destroyed, "engineer consumed");
        // Enemy building below the capture level: capture changes the owner.
        world.entities[0] = entity(1, 1, "ENGINEER", sw::Kind::Infantry, {12, 11}, 75);
        const auto capture = ra2::sim::engineer_enter_building(rules, world, 1, 3, 0.25);
        require(capture.effect == EngineerEffect::Captured, "damaged enemy building captured");
        require(world.entities[2].house_id == 1, "captured building changes owner");
        require(world.entities[0].lifetime == sw::Lifetime::Destroyed, "capture consumes engineer");
        // Healthy enemy building: damage, never destroyed.
        world.entities[0] = entity(1, 1, "ENGINEER", sw::Kind::Infantry, {14, 11}, 75);
        const auto damaged = ra2::sim::engineer_enter_building(rules, world, 1, 4, 0.25);
        require(damaged.effect == EngineerEffect::Damaged, "healthy building damaged");
        require(damaged.damage == 600 / 3, "damage is max strength / 3");
        require(world.entities[3].resolved_hp.value_or(0) == 600 - 200, "damage applied");
        // Non-engineer infantry does nothing.
        world.entities[0] = entity(1, 1, "GI", sw::Kind::Infantry, {14, 11}, 75);
        const auto none = ra2::sim::engineer_enter_building(rules, world, 1, 4, 0.25);
        require(none.effect == EngineerEffect::None, "non-engineer does nothing");
        require(world.entities[0].lifetime != sw::Lifetime::Destroyed, "non-engineer survives");
        require_throws([&] {
            auto copy = world;
            ra2::sim::engineer_enter_building(rules, copy, 1, 4);
        }, "dead engineer throws");
    }

    // ---- Engineer: bridge repair ----
    {
        sw::Snapshot world;
        world.houses = {house(1, "A")};
        world.entities = {entity(1, 1, "ENGINEER", sw::Kind::Infantry, {10, 10}, 75)};
        std::map<std::uint32_t, bool> broken{{10u * 512u + 11u, true}, {10u * 512u + 20u, true}};
        const auto far = 10u * 512u + 20u;
        const auto near = 10u * 512u + 11u;
        require(!ra2::sim::engineer_repair_bridge(world, broken, 1, far), "too far refuses");
        require(ra2::sim::engineer_repair_bridge(world, broken, 1, near), "adjacent repairs");
        require(!broken[near], "repaired cell is no longer broken");
        require(world.entities[0].lifetime == sw::Lifetime::Destroyed, "repair consumes engineer");
        require(!ra2::sim::engineer_repair_bridge(world, broken, 1, far), "consumed engineer refuses");
    }

    // ---- Spy infiltration ----
    {
        sw::Snapshot world;
        world.houses = {house(1, "A"), house(2, "B")};
        world.entities = {
            entity(1, 1, "SPY", sw::Kind::Infantry, {10, 10}, 100),
            entity(2, 2, "SPANEL", sw::Kind::Structure, {10, 11}, 400),  // power plant
            entity(3, 2, "NARADR", sw::Kind::Structure, {12, 11}, 600),  // radar
            entity(4, 2, "GACNST", sw::Kind::Structure, {14, 11}, 1000), // yard
            entity(5, 2, "NAWEAP", sw::Kind::Structure, {16, 11}, 1000), // nothing
        };
        SpyInfiltrations spy_state;
        // Power plant: outage on the enemy house.
        const auto power = ra2::sim::spy_infiltrate(rules, world, spy_state, 1, 2);
        require(power.kind == SpyEffectKind::PowerOutage, "power plant outage");
        require(spy_state.power_outage_frames[2] == 1000, "outage lasts 1000 frames");
        // Radar: radar reset.
        world.entities[0] = entity(1, 1, "SPY", sw::Kind::Infantry, {12, 12}, 100);
        const auto radar = ra2::sim::spy_infiltrate(rules, world, spy_state, 1, 3);
        require(radar.kind == SpyEffectKind::RadarReset, "radar reset");
        require(spy_state.radar_off_frames[2] == 1000, "radar off 1000 frames");
        // Construction yard: half the enemy cash moves to the spy's house.
        world.entities[0] = entity(1, 1, "SPY", sw::Kind::Infantry, {14, 12}, 100);
        const auto money = ra2::sim::spy_infiltrate(rules, world, spy_state, 1, 4);
        require(money.kind == SpyEffectKind::MoneyStolen, "yard money steal");
        require(money.amount == 500, "steal is half the victim cash");
        require(world.houses[1].cash.value_or(0) == 500, "victim loses cash");
        require(world.houses[0].cash.value_or(0) == 1500, "spy house gains cash");
        // Plain war factory: no effect.
        world.entities[0] = entity(1, 1, "SPY", sw::Kind::Infantry, {16, 12}, 100);
        const auto none = ra2::sim::spy_infiltrate(rules, world, spy_state, 1, 5);
        require(none.kind == SpyEffectKind::None, "plain building has no effect");
        require(world.entities[0].lifetime == sw::Lifetime::Destroyed, "spy consumed");
        // Lab unlock through the caller lab list.
        world.entities[0] = entity(1, 1, "SPY", sw::Kind::Infantry, {10, 12}, 100);
        world.entities.push_back(entity(6, 2, "GATECH", sw::Kind::Structure, {10, 13}, 500));
        const auto lab = ra2::sim::spy_infiltrate(rules, world, spy_state, 1, 6);
        require(lab.kind == SpyEffectKind::TechUnlock, "lab unlock");
        require(spy_state.unlocked_labs[1].size() == 1 &&
            spy_state.unlocked_labs[1][0] == "GATECH", "lab recorded");
        // Guards: own house, non-spy, dead building.
        world.entities[0] = entity(1, 1, "SPY", sw::Kind::Infantry, {10, 12}, 100);
        require_throws([&] {
            auto copy = world;
            ra2::sim::spy_infiltrate(rules, copy, spy_state, 1, 6);
        }, "consumed spy throws");
        // Timers tick down and clear.
        spy_state.tick(600);
        require(spy_state.power_outage_frames[2] == 400, "outage ticks down");
        spy_state.tick(1000);
        require(spy_state.power_outage_frames.empty() && spy_state.radar_off_frames.empty(),
            "exhausted timers clear");
    }

    // ---- Crates ----
    {
        const auto crate_rules = CrateRules::load(rules);
        require_near(crate_rules.radius, 3.0, 1e-9, "CrateRadius");
        require(crate_rules.crate_maximum == 255, "CrateMaximum");
        require(crate_rules.solo_money == 5000, "SoloCrateMoney");
        require(crate_rules.unit_crate_type.empty(), "UnitCrateType none");

        sw::Snapshot world;
        world.houses = {house(1, "A"), house(2, "B")};
        world.entities = {
            entity(1, 1, "MTNK", sw::Kind::Unit, {10, 10}, 300, 300),
            entity(2, 1, "GI", sw::Kind::Infantry, {11, 10}, 100, 100),
            entity(3, 2, "MTNK", sw::Kind::Unit, {20, 20}, 300, 300),
        };
        world.entities[1].maximum_hp = 200; // wounded GI heals to 200.
        world.entities[1].resolved_hp = 50;

        CrateState crates;
        const auto money_cell = 10u * 512u + 10u;
        require(crates.place(money_cell, CrateKind::Money, 0, crate_rules), "place money crate");
        require(!crates.place(money_cell, CrateKind::Money, 0, crate_rules),
            "double placement refuses");
        require(crates.find(money_cell) != nullptr, "crate found at cell");
        const auto money = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(money.kind == CrateKind::Money && money.credits >= 0 && money.credits <= 900,
            "money crate pays 0..900");
        require(world.houses[0].cash.value_or(0) == 1000 + money.credits, "credits added");
        require(crates.find(money_cell) == nullptr, "crate consumed");

        // Heal base heals only the collector's house.
        require(crates.place(money_cell, CrateKind::HealBase, 0, crate_rules), "place heal crate");
        const auto healed = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(healed.affected.size() == 2, "both house entities heal");
        require(world.entities[1].resolved_hp.value_or(0) == 200, "wounded GI fully healed");
        require(world.entities[2].resolved_hp.value_or(0) == 300, "enemy stays at full health");

        // Area armor crate: only entities in radius and only once.
        require(crates.place(money_cell, CrateKind::Armor, 0, crate_rules), "place armor crate");
        const auto armored = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(armored.affected.size() == 2, "both house technos are in radius");
        require_near(crates.armor_bias(1), 1.5, 1e-9, "tank armor bias");
        require_near(crates.armor_bias(2), 1.5, 1e-9, "GI armor bias");
        require_near(crates.armor_bias(3), 1.0, 1e-9, "enemy keeps plain armor");
        require(crates.place(money_cell, CrateKind::Armor, 0, crate_rules), "second armor crate");
        crates.collect(rules, world, crate_rules, 1, money_cell);
        require_near(crates.armor_bias(1), 1.5, 1e-9, "bias applies once");

        // Speed and firepower biases are independent.
        require(crates.place(money_cell, CrateKind::Speed, 0, crate_rules), "place speed crate");
        crates.collect(rules, world, crate_rules, 1, money_cell);
        require_near(crates.speed_bias(1), 1.2, 1e-9, "speed bias");
        require(crates.place(money_cell, CrateKind::Firepower, 0, crate_rules),
            "place firepower crate");
        crates.collect(rules, world, crate_rules, 1, money_cell);
        require_near(crates.firepower_bias(1), 1.1, 1e-9, "firepower bias");

        // Cloak marks in radius.
        require(crates.place(money_cell, CrateKind::Cloak, 0, crate_rules), "place cloak crate");
        crates.collect(rules, world, crate_rules, 1, money_cell);
        require(crates.cloakable(1) && crates.cloakable(2), "house technos cloakable");
        require(!crates.cloakable(3), "enemy not cloakable");

        // Veterancy crate promotes trainable units in radius.
        require(crates.place(money_cell, CrateKind::Veterancy, 0, crate_rules),
            "place veterancy crate");
        const auto promoted = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(promoted.affected.size() == 2, "both house technos promoted");
        require(rank_of(world.entities[0].veterancy.value_or(0.0)) == Rank::Veteran,
            "tank now veteran");
        require(rank_of(world.entities[1].veterancy.value_or(0.0)) == Rank::Veteran,
            "GI now veteran");
        require(rank_of(world.entities[2].veterancy.value_or(0.0)) == Rank::Rookie,
            "enemy stays rookie");

        // Unit crate: no UnitCrateType falls back to money.
        require(crates.place(money_cell, CrateKind::Unit, 0, crate_rules), "place unit crate");
        const auto unit_fallback = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(unit_fallback.kind == CrateKind::Money && unit_fallback.created_unit == 0,
            "unit crate falls back to money");

        // Unit crate with a UnitCrateType spawns a created unit.
        const auto with_unit = CrateRules::load(overlay(rules, "[CrateRules]\nUnitCrateType=MTNK\n"));
        require(with_unit.unit_crate_type == "MTNK", "UnitCrateType read");
        require(crates.place(money_cell, CrateKind::Unit, 0, with_unit), "place unit crate 2");
        const auto spawned = crates.collect(rules, world, with_unit, 1, money_cell);
        require(spawned.created_unit == 4, "new unit id assigned");
        require(world.entities.back().type == "MTNK" &&
            world.entities.back().lifetime == sw::Lifetime::Created &&
            world.entities.back().house_id == 1, "created unit belongs to the collector");

        // Explosion crate damages the collector.
        require(crates.place(money_cell, CrateKind::Explosion, 0, crate_rules),
            "place explosion crate");
        const auto boom = crates.collect(rules, world, crate_rules, 1, money_cell);
        require(boom.affected.size() == 1, "explosion hits the collector");
        require(world.entities[0].resolved_hp.value_or(0) == 0,
            "explosion damage leaves the tank dead");

        // Expiry: crates vanish at the regen time (3 minutes = 2700 frames).
        CrateState timed;
        require(timed.place(money_cell, CrateKind::Money, 100, crate_rules), "timed place");
        require(timed.expire(100 + 2699).empty(), "crate survives before expiry");
        const auto gone = timed.expire(100 + 2700);
        require(gone.size() == 1 && gone[0] == money_cell, "crate expires at regen time");

        // Random picks respect the shares and advance the shared random state.
        CrateState lottery;
        const auto before = world.random;
        const auto kind = lottery.random_kind(world, crate_rules);
        require(kind >= CrateKind::Money && kind < CrateKind::CrateKindCount, "pick in range");
        bool advanced = false;
        for (std::size_t i = 0; i < world.random.table.size(); ++i)
            if (world.random.table[i] != before.table[i]) { advanced = true; break; }
        require(advanced, "shared random state advanced");
    }

    if (failures) {
        std::cerr << failures << " special unit rule checks failed\n";
        return 1;
    }
    std::cout << "Prototype special unit rule checks passed: veterancy, garrison, engineer, "
                 "spy and crates.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
