// Regression: the crate radius is strict in 3D, and an object without a
// position is skipped instead of dereferenced.
#include "../sim/prototype_special.h"
#include <iostream>

using namespace ra2;
using namespace ra2::sim;

namespace world = ra2::session::world;

namespace {
int failures = 0;
void check(bool ok, const char* why) { if (!ok) { std::cerr << "FAIL: " << why << char(10); ++failures; } }

const rules::IniFile& test_rules() {
    static const rules::IniFile ini = []() {
        rules::IniFile r;
        r.parse("[CrateRules]\n");
        return r;
    }();
    return ini;
}

world::Entity unit_at(int x, int y, int z) {
    world::Entity e;
    e.id = 2;
    e.house_id = 102;
    e.kind = world::Kind::Unit;
    e.lifetime = world::Lifetime::Placed;
    e.cell = ra2map::Cell{50, 50};
    e.position = sim::Coord{x, y, z};
    return e;
}

// True when the pickup reached the unit (the armor multiplier moved).
bool reached(int x, int y, int z) {
    world::Snapshot w;
    w.random = sim::Random(0).state();
    w.entities.push_back(unit_at(x, y, z));
    CrateState crates;
    crates.place(50 * 512 + 50, CrateKind::Armor, 0, CrateRules{});
    const auto applied = crates.collect(test_rules(), w, CrateRules{}, 2, 50 * 512 + 50);
    return !applied.affected.empty() &&
        w.entities[0].armor_multiplier.value_or(1.0) != 1.0;
}

} // namespace

int main() {
    // Same cell center: inside the 3.0 cell radius.
    check(reached(50 * 256 + 128, 50 * 256 + 128, 0), "center pickup");
    // One cell away: still inside.
    check(reached(51 * 256 + 128, 50 * 256 + 128, 0), "one cell pickup");
    // Exactly at the 3.0 cell radius: strictly less excludes it.
    check(!reached(53 * 256 + 128, 50 * 256 + 128, 0), "radius edge excluded");
    // Vertical separation counts toward the 3D distance.
    check(!reached(50 * 256 + 128, 50 * 256 + 128, 3 * 256), "vertical excluded");
    // An object without a position is skipped, not dereferenced.
    {
        world::Snapshot w;
        w.random = sim::Random(0).state();
        world::Entity e;
        e.id = 2;
        e.house_id = 102;
        e.kind = world::Kind::Unit;
        e.lifetime = world::Lifetime::Placed;
        e.cell = ra2map::Cell{50, 50};
        e.position = std::nullopt;
        w.entities.push_back(e);
        CrateState crates;
        crates.place(50 * 512 + 50, CrateKind::Armor, 0, CrateRules{});
        const auto applied = crates.collect(test_rules(), w, CrateRules{}, 2, 50 * 512 + 50);
        check(applied.affected.empty(), "missing position skipped");
    }
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "crate radius boundary checks passed\n";
    return 0;
}
