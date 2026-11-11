// Crate multipliers: 1.5 armor and 1.1 firepower are the crate constants
// in prototype.cpp (CrateData stand-ins, TODO-ORIGINAL 00481A00).
// Runtime check: armor and firepower crates must reach the damage path
// through the resolved Entity fields, not only the CrateState biases.
#include <iostream>
#include <stdexcept>
#include "../sim/prototype_special.h"

using namespace ra2;
using namespace ra2::sim;
namespace world = ra2::session::world;

namespace {
void check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
} // namespace

int main() {
    try {
        rules::IniFile rules;
        rules.parse("[CrateRules]\n[Powerups]\nArmor=10,ARMOR,yes,175%\nFirepower=10,FIREPOWR,yes,250%\n");
        const auto crate_rules = CrateRules::load(rules);
        session::world::Snapshot w;
        w.random = sim::Random(0).state();

        // One live unit inside the crate radius.
        world::Entity unit;
        unit.id = 2;
        unit.house_id = 102;
        unit.kind = session::world::Kind::Unit;
        unit.lifetime = session::world::Lifetime::Placed;
        unit.cell = ra2map::Cell{50, 50};
        unit.position = sim::Coord{50 * 256 + 128, 50 * 256 + 128, 0};
        unit.armor_multiplier = 1.0;
        unit.firepower_multiplier = 1.0;
        w.entities.push_back(unit);

        CrateState crates;
        check(crates.place(50 * 512 + 50, CrateKind::Armor, 0, crate_rules),
              "armor crate placement failed");
        const auto armor = crates.collect(rules, w, crate_rules, 2, 50 * 512 + 50);
        check(armor.kind == CrateKind::Armor, "wrong crate kind");
        check(w.entities[0].armor_multiplier == 1.75,
              "armor crate did not write the resolved armor multiplier");
        check(w.entities[0].firepower_multiplier == 1.0,
              "armor crate must not touch firepower");

        // A second crate state for the firepower bonus.
        CrateState firepower_crates;
        check(firepower_crates.place(50 * 512 + 50, CrateKind::Firepower, 0, crate_rules),
              "firepower crate placement failed");
        const auto firepower = firepower_crates.collect(rules, w, crate_rules, 2, 50 * 512 + 50);
        check(firepower.kind == CrateKind::Firepower, "wrong crate kind");
        check(w.entities[0].firepower_multiplier == 2.5,
              "firepower crate did not write the resolved firepower multiplier");

        // A loaded upgraded actor must not gain the bonus again: place a
        // fresh crate, then collect it onto the already upgraded actor.
        check(crates.place(50 * 512 + 50, CrateKind::Armor, 0, crate_rules),
              "repeated armor crate placement failed");
        const auto repeat = crates.collect(rules, w, crate_rules, 2, 50 * 512 + 50);
        check(repeat.affected.empty(),
              "repeated pickup rewrote an existing crate bonus");
        check(w.entities[0].armor_multiplier == 1.75,
              "repeated pickup changed the resolved armor multiplier");

        std::cout << "crate damage connection checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
