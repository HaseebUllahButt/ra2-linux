// Prototype shroud and fog of war checks. Builds small worlds, ticks them
// through the public prototype_shroud.h API and reads cells back. The last
// block reads the installed base RULES.INI for real Sight= and gap values.
#include "prototype_shroud.h"
#include "fs/mix_bootstrap.h"

#include <cstdint>
#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace sw = ra2::session::world;
using ra2::sim::PrototypeShroud;
using Vision = ra2::sim::PrototypeShroud::Vision;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ra2map::Cell cell(std::uint16_t x, std::uint16_t y) { return ra2map::Cell{x, y}; }

sw::House house(std::uint32_t id, const char* name,
                sw::HouseRole role = sw::HouseRole::Player) {
    sw::House result;
    result.id = id;
    result.name = name;
    result.country = name;
    result.role = role;
    return result;
}

sw::Entity actor(std::uint32_t id, std::uint32_t owner, const char* type, sw::Kind kind,
                 std::uint16_t x, std::uint16_t y) {
    sw::Entity entity;
    entity.id = id;
    entity.house_id = owner;
    entity.type = type;
    entity.kind = kind;
    entity.lifetime = sw::Lifetime::Placed;
    entity.cell = ra2map::Cell{x, y};
    entity.on_map = true;
    return entity;
}

// The clipped square of sight range two, from the RA1 RadiusOffset table.
void check_range_two_diamond(PrototypeShroud& shroud) {
    require(shroud.cell_revealed_to(1, cell(20, 20)), "center revealed");
    require(shroud.cell_revealed_to(1, cell(22, 20)), "east edge");
    require(shroud.cell_revealed_to(1, cell(20, 22)), "south edge");
    require(shroud.cell_revealed_to(1, cell(21, 21)), "near diagonal");
    require(!shroud.cell_revealed_to(1, cell(22, 22)), "corner stays cut");
    require(!shroud.cell_revealed_to(1, cell(23, 20)), "past range stays hidden");
    require(shroud.cell_visible_to(1, cell(20, 20)), "center visible");
    require(!shroud.cell_visible_to(2, cell(20, 20)), "other house stays blind");
}

// Ticks the same snapshot again: every probed verdict must not move.
void check_tick_is_idempotent(PrototypeShroud& shroud, const sw::Snapshot& world,
                              const ra2::rules::IniFile& rules) {
    const ra2map::Cell probes[] = {{10, 10}, {12, 10}, {30, 30}, {0, 0}, {39, 39}};
    for (const auto where : probes) {
        const auto first = shroud.vision(1, where);
        shroud.tick(world, rules);
        require(shroud.vision(1, where) == first, "repeat tick changed a verdict");
    }
}
} // namespace

int main(int argc, char** argv) try {
    // Sight diamond shapes, straight from the RA1 RadiusOffset rings.
    {
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(20, 20), 2);
        check_range_two_diamond(shroud);
    }
    {
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(20, 20), 1);
        require(shroud.cell_visible_to(1, cell(21, 21)), "range one diagonal");
        require(!shroud.cell_visible_to(1, cell(22, 20)), "range one stops at the edge");
    }
    {
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(20, 20), 0);
        shroud.sight_from(1, cell(20, 20), -3);
        shroud.sight_from(0, cell(20, 20), 4);
        require(shroud.vision(1, cell(20, 20)) == Vision::Hidden,
            "zero or negative range does nothing");
        require(shroud.vision(0, cell(20, 20)) == Vision::Hidden, "house zero never sights");
    }
    // Grid edges clamp; unknown houses and outside cells read as Hidden.
    {
        PrototypeShroud shroud(4, 4);
        shroud.sight_from(1, cell(0, 0), 3);
        require(shroud.cell_visible_to(1, cell(0, 0)), "corner cell reveals");
        require(shroud.cell_visible_to(1, cell(2, 1)), "clamped diagonal");
        require(!shroud.cell_visible_to(1, cell(3, 3)), "range three corner is cut");
        require(!shroud.cell_revealed_to(1, cell(4, 0)), "outside width reads hidden");
        require(!shroud.cell_revealed_to(1, cell(65535, 0)), "wrapped cell reads hidden");
        require(shroud.vision(9, cell(0, 0)) == Vision::Hidden, "unknown house reads hidden");
        require(shroud.width() == 4 && shroud.height() == 4, "grid size round trip");
    }
    // Original CellSpread counts 007A5908 for every range 0..10, and the cap
    // at ten cells from RevealArea 0054A110.
    {
        const int counts[] = {1, 9, 21, 37, 61, 89, 121, 161, 205, 253, 309};
        for (int range = 0; range <= 11; ++range) {
            PrototypeShroud shroud(40, 40);
            shroud.sight_from(1, cell(20, 20), range);
            int revealed = 0;
            for (std::uint16_t y = 0; y < 40; ++y)
                for (std::uint16_t x = 0; x < 40; ++x)
                    revealed += shroud.cell_revealed_to(1, cell(x, y)) ? 1 : 0;
            const int expected = range == 0 ? 0 : counts[std::min(range, 10)];
            require(revealed == expected, "sight area differs from the original CellSpread count");
        }
        // Spot offsets from the table: (3,1) is in range 3, (3,2) is not.
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(20, 20), 3);
        require(shroud.cell_revealed_to(1, cell(23, 21)), "CellSpread range three keeps (3,1)");
        require(!shroud.cell_revealed_to(1, cell(23, 22)), "CellSpread range three drops (3,2)");
    }
    // A building sees from the centre cell the caller supplies.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[YARD]\nSight=2\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player")};
        world.entities = {actor(1, 1, "YARD", sw::Kind::Structure, 10, 10)};
        shroud.tick(world, rules, [](const sw::Entity& entity) {
            return std::optional<ra2map::Cell>(ra2map::Cell{
                static_cast<std::uint16_t>(entity.cell->x + 2),
                static_cast<std::uint16_t>(entity.cell->y + 2)});
        });
        require(shroud.cell_visible_to(1, cell(14, 12)), "sight from the footprint centre");
        require(!shroud.cell_visible_to(1, cell(9, 10)), "not from the anchor cell");
    }
    // An ally of the gap owner keeps its view.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[TANK]\nSight=4\n[GAPGEN]\nGapGenerator=yes\nGapRadiusInCells=3\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player"), house(2, "Enemy"), house(3, "Friend")};
        world.houses[0].original_index = 0;
        world.houses[1].original_index = 1;
        world.houses[2].original_index = 2;
        world.houses[1].alliance_mask_5674 = 1u << 2;
        world.entities = {actor(1, 1, "TANK", sw::Kind::Unit, 10, 10),
            actor(2, 2, "GAPGEN", sw::Kind::Structure, 12, 10),
            actor(3, 3, "TANK", sw::Kind::Unit, 12, 10)};
        shroud.tick(world, rules);
        require(!shroud.cell_revealed_to(1, cell(12, 10)), "gap hides the enemy");
        require(shroud.cell_visible_to(3, cell(12, 10)), "gap spares the owner's ally");
    }
    // Fog: sight follows the actor, explored ground stays.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[TANK]\nSight=4\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player")};
        world.entities = {actor(1, 1, "TANK", sw::Kind::Unit, 10, 10)};
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(14, 10)), "sight edge east");
        require(!shroud.cell_visible_to(1, cell(18, 10)), "past sight east");
        world.entities[0].cell = cell(30, 30);
        shroud.tick(world, rules);
        require(shroud.cell_revealed_to(1, cell(10, 10)), "old ground stays explored");
        require(!shroud.cell_visible_to(1, cell(10, 10)), "old ground loses sight");
        require(shroud.cell_visible_to(1, cell(30, 30)), "new ground visible");
        world.entities[0].lifetime = sw::Lifetime::Destroyed;
        shroud.tick(world, rules);
        require(shroud.cell_revealed_to(1, cell(30, 30)), "dead actor ground stays explored");
        require(!shroud.cell_visible_to(1, cell(30, 30)), "dead actor ground loses sight");
        check_tick_is_idempotent(shroud, world, rules);
    }
    // Gap generator: hides its radius from enemies, keeps the explored state
    // underneath, and returns it when it dies.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[TANK]\nSight=4\n[GAPGEN]\nGapGenerator=yes\nGapRadiusInCells=3\nSight=1\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player"), house(2, "Enemy")};
        world.entities = {actor(1, 1, "TANK", sw::Kind::Unit, 10, 10),
            actor(2, 2, "GAPGEN", sw::Kind::Structure, 12, 10)};
        shroud.tick(world, rules);
        require(!shroud.cell_visible_to(1, cell(12, 10)), "gap hides its center");
        require(shroud.vision(1, cell(12, 10)) == Vision::Hidden, "gap reads as black shroud");
        require(!shroud.cell_visible_to(1, cell(14, 10)), "gap hides the sight overlap");
        require(shroud.cell_visible_to(1, cell(8, 10)), "outside the gap stays visible");
        require(shroud.cell_visible_to(2, cell(12, 10)), "owner sees its own gap generator");
        world.entities[1].lifetime = sw::Lifetime::Destroyed;
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(12, 10)), "gap loss returns the old view");
        require(shroud.cell_visible_to(1, cell(14, 10)), "gap loss returns the sight overlap");
        check_tick_is_idempotent(shroud, world, rules);
    }
    // Spy satellite: one live SpySat=yes building reveals everything; losing
    // it reshrouds the map and sight rebuilds from the remaining actors.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[SAT]\nSpySat=yes\nSight=1\n[TANK]\nSight=2\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player"), house(2, "Enemy")};
        world.entities = {actor(1, 1, "SAT", sw::Kind::Structure, 5, 5),
            actor(2, 1, "TANK", sw::Kind::Unit, 6, 6),
            actor(3, 2, "TANK", sw::Kind::Unit, 20, 20)};
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(39, 39)), "satellite reveals the far corner");
        require(shroud.cell_revealed_to(1, cell(39, 39)), "satellite explores the far corner");
        require(!shroud.cell_visible_to(2, cell(39, 39)), "the enemy stays blind");
        world.entities[0].lifetime = sw::Lifetime::Destroyed;
        shroud.tick(world, rules);
        require(!shroud.cell_revealed_to(1, cell(39, 39)), "satellite loss reshrouds");
        require(shroud.cell_visible_to(1, cell(5, 5)), "sight rebuilds after the loss");
        require(shroud.cell_visible_to(2, cell(20, 20)), "the enemy keeps its own sight");
        check_tick_is_idempotent(shroud, world, rules);
    }
    // Spy satellite eligibility, per base FUN_004F2FC0: a dead, inactive,
    // limbo or mission-0x13 satellite must not grant the view, and
    // losing the last eligible one reshrouds. Restoring an eligible one
    // brings the full map view back. The power check does not exist in this
    // path, so a house with no power output still gets the full reveal.
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[SAT]\nSpySat=yes\nPowered=yes\nSight=1\n[TANK]\nSight=2\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player")};
        world.entities = {actor(1, 1, "SAT", sw::Kind::Structure, 5, 5),
            actor(2, 1, "TANK", sw::Kind::Unit, 6, 6)};
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(39, 39)), "healthy satellite reveals the map");

        const auto restore = [&]() {
            auto& sat = world.entities[0];
            sat.resolved_hp = 100;
            sat.active = true;
            sat.in_limbo = false;
            sat.mission = 0;
        };
        const auto expect_blind = [&](const char* why) {
            shroud.tick(world, rules);
            require(!shroud.cell_visible_to(1, cell(39, 39)), why);
            require(!shroud.cell_revealed_to(1, cell(39, 39)), "loss reshrouds the explored map");
            require(shroud.cell_visible_to(1, cell(8, 6)), "tank sight stays after the loss");
        };
        const auto expect_full = [&](const char* why) {
            shroud.tick(world, rules);
            require(shroud.cell_visible_to(1, cell(39, 39)), why);
        };

        world.entities[0].resolved_hp = 0;
        expect_blind("zero HP satellite stops the full map view");
        restore();
        expect_full("restored satellite restores the full view");

        world.entities[0].active = false;
        expect_blind("inactive satellite stops the full map view");
        restore();
        expect_full("reactivated satellite restores the full view");

        world.entities[0].in_limbo = true;
        expect_blind("limbo satellite stops the full map view");
        restore();
        expect_full("un-limbo satellite restores the full view");

        world.entities[0].mission = 0x13;
        expect_blind("mission 0x13 satellite stops the full map view");
        restore();
        expect_full("idle-mission satellite restores the full view");

        // No power gate: an unpowered house keeps the full satellite view.
        world.houses[0].power_output = 0;
        world.houses[0].power_drain = 100;
        expect_full("unpowered house keeps the full satellite view");
        world.houses[0].power_output = std::nullopt;
        world.houses[0].power_drain = std::nullopt;
    }
    {
        PrototypeShroud shroud(40, 40);
        ra2::rules::IniFile rules;
        rules.parse("[BLIND]\nSight=0\n[MYSTERY]\n");
        sw::Snapshot world;
        world.houses = {house(1, "Player")};
        world.entities = {actor(1, 1, "BLIND", sw::Kind::Unit, 10, 10)};
        shroud.tick(world, rules);
        require(shroud.vision(1, cell(10, 10)) == Vision::Hidden, "sight zero reveals nothing");
        world.entities[0].type = "MYSTERY";
        shroud.tick(world, rules);
        require(shroud.sight_range(rules, "MYSTERY") == 0, "missing Sight reads as zero");
        require(shroud.vision(1, cell(10, 10)) == Vision::Hidden,
            "missing Sight reveals nothing (006D9B40 default)");
        rules.parse("[BLIND]\nSight=0\n[MYSTERY]\nSight=1\n");
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(10, 10)), "Sight=1 reveals the cell");
        world.entities[0].lifetime = sw::Lifetime::Created;
        shroud.tick(world, rules);
        require(!shroud.cell_visible_to(1, cell(10, 10)), "unplaced actor never sights");
        world.entities[0].lifetime = sw::Lifetime::Placed;
        world.entities[0].on_map = false;
        shroud.tick(world, rules);
        require(!shroud.cell_visible_to(1, cell(10, 10)), "off-map actor never sights");
        world.entities[0].on_map = true;
        world.entities[0].house_id = 0;
        shroud.tick(world, rules);
        require(!shroud.cell_visible_to(1, cell(10, 10)), "terrain never sights");
    }
    // Manual gap primitive: hides from every other known house at once.
    {
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(20, 20), 3);
        shroud.sight_from(2, cell(30, 30), 3);
        shroud.shroud_from(3, cell(20, 20), 2);
        require(shroud.vision(1, cell(20, 20)) == Vision::Hidden, "manual gap hides house one");
        require(shroud.vision(2, cell(20, 20)) == Vision::Hidden, "manual gap hides house two");
        require(shroud.vision(3, cell(20, 20)) == Vision::Hidden, "owner never saw the cell");
        require(shroud.vision(1, cell(23, 20)) == Vision::Visible, "gap edge stops at radius two");
    }
    // Full reshroud primitive forgets one house only.
    {
        PrototypeShroud shroud(40, 40);
        shroud.sight_from(1, cell(10, 10), 3);
        shroud.sight_from(2, cell(10, 10), 3);
        shroud.reshroud_all(1);
        require(shroud.vision(1, cell(10, 10)) == Vision::Hidden, "reshroud forgets the house");
        require(shroud.cell_visible_to(2, cell(10, 10)), "reshroud keeps the other house");
    }

    // Real base rules: real Sight= values, the gap generator and the spy
    // satellite uplink, straight from the installed RULES.INI.
    if (argc == 2) {
        ra2::fs::MixFileSystem files(argv[1]);
        files.mount_language(ra2::fs::GameVariant::RedAlert2);
        files.bootstrap(ra2::fs::GameVariant::RedAlert2);
        files.mount_overlays();
        files.mount_resources();
        const auto bytes = files.read_file("RULES.INI");
        require(bytes.has_value(), "Installed base RULES.INI is missing");
        ra2::rules::IniFile rules;
        rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        PrototypeShroud shroud(100, 100);
        require(shroud.sight_range(rules, "MTNK") == 8, "Grizzly tank Sight");
        require(shroud.sight_range(rules, "GAGAP") == 5, "gap generator Sight");
        require(rules.read_bool("GAGAP", "GapGenerator"), "gap generator flag");
        require(rules.read_integer("GAGAP", "GapRadiusInCells") == 10, "gap radius in cells");
        require(rules.read_bool("GASPYSAT", "SpySat"), "spy satellite flag");
        require(rules.read_bool("GASPYSAT", "Powered"), "the satellite type is powered");

        sw::Snapshot world;
        world.houses = {house(1, "Americans"), house(2, "Russians")};
        world.entities = {actor(1, 1, "MTNK", sw::Kind::Unit, 50, 50)};
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(58, 50)), "Grizzly sight eight east");
        require(!shroud.cell_visible_to(1, cell(58, 58)), "sight eight corner is cut");
        world.entities.push_back(actor(2, 2, "GAGAP", sw::Kind::Structure, 45, 45));
        shroud.tick(world, rules);
        require(!shroud.cell_visible_to(1, cell(50, 50)), "gap generator hides its overlap");
        require(shroud.cell_visible_to(1, cell(58, 50)), "sight outside the gap survives");
        require(shroud.cell_visible_to(2, cell(45, 45)), "the gap owner still sees its generator");
        world.entities.push_back(actor(3, 1, "GASPYSAT", sw::Kind::Structure, 90, 90));
        shroud.tick(world, rules);
        require(shroud.cell_visible_to(1, cell(50, 50)), "satellite sees through the gap");
        require(!shroud.cell_visible_to(2, cell(39, 39)), "the enemy stays blind without one");
        std::cout << "Base RULES.INI shroud values verified: MTNK sight, gap generator, spy satellite.\n";
    }
    std::cout << "Prototype shroud spot checks passed: sight diamond, fog, gap generator, "
                 "spy satellite, edge clamps and rules defaults.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
