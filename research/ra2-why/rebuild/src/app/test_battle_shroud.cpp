// Battle shroud checks: entity visibility, fog switch, footprints, scene and
// radar masks. With a game directory the last block reads base RULES.INI.
#include "battle_shroud.h"
#include "../fs/mix_bootstrap.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace ra2;
using namespace ra2::app;
using namespace ra2::session::world;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ra2map::Cell cell(int x, int y) {
    return ra2map::Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
}
House house(std::uint32_t id, HouseRole role = HouseRole::Player) {
    House result;
    result.id = id;
    result.role = role;
    result.original_index = std::int32_t(id);
    return result;
}
Entity actor(std::uint32_t id, std::uint32_t owner, Kind kind, const char* type, int x, int y) {
    Entity result;
    result.id = id;
    result.house_id = owner;
    result.kind = kind;
    result.type = type;
    result.lifetime = Lifetime::Placed;
    result.cell = cell(x, y);
    result.on_map = true;
    return result;
}
Snapshot battle() {
    Snapshot world;
    world.houses = {house(1), house(2), house(3, HouseRole::Neutral)};
    world.entities = {actor(1, 1, Kind::Unit, "TANK", 50, 50),
        actor(2, 2, Kind::Unit, "TANK", 53, 50),   // In sight.
        actor(3, 2, Kind::Unit, "TANK", 90, 90),   // Far in the black.
        actor(4, 3, Kind::Structure, "CIVHUT", 48, 50),
        actor(5, 0, Kind::Terrain, "TREE", 95, 95)};
    for (int y = 40; y < 70; ++y)
        for (int x = 40; x < 70; ++x) world.cells[std::uint32_t(y * 512 + x)].allocated = true;
    return world;
}
rules::IniFile rules_for_test() {
    rules::IniFile rules;
    rules.parse("[TANK]\nSight=4\n[YARD]\nSight=3\n[CIVHUT]\n");
    return rules;
}

void visibility() {
    auto world = battle();
    const auto rules = rules_for_test();
    BattleShroud shroud;
    shroud.update(world, rules);
    require(shroud.entity_visible(1, world.entities[0]), "own unit shows");
    require(shroud.entity_visible(1, world.entities[1]), "enemy in sight shows");
    require(!shroud.entity_visible(1, world.entities[2]), "enemy in the black is hidden");
    require(shroud.entity_visible(1, world.entities[3]), "neutral building in sight shows");
    require(!shroud.entity_visible(1, world.entities[4]), "tree in the black is hidden");
    require(shroud.entity_visible(2, world.entities[2]), "enemy sees its own unit");
    require(shroud.entity_visible(2, world.entities[0]), "the near enemy sees our unit too");
    require(!shroud.revealed(2, cell(45, 50)), "the enemy sight ends before our far side");
    require(shroud.revealed(1, cell(54, 50)) && !shroud.revealed(1, cell(55, 50)),
        "sight four ends at four cells");

    // Effect coordinates use the same test: 256 leptons per cell.
    require(shroud.coord_shown(1, sim::Coord{53 * 256 + 128, 50 * 256 + 128, 0}),
        "coordinate in sight shows");
    require(!shroud.coord_shown(1, sim::Coord{90 * 256 + 128, 90 * 256 + 128, 0}),
        "coordinate in the black hides");
    require(!shroud.coord_shown(1, sim::Coord{-5, 10, 0}), "negative coordinate hides");
    // Base skirmish has no fog: explored ground keeps showing objects.
    world.entities[0].cell = cell(60, 60);
    shroud.update(world, rules);
    require(shroud.revealed(1, cell(53, 50)) && !shroud.visible_now(1, cell(53, 50)),
        "old ground stays explored, out of sight");
    require(shroud.entity_visible(1, world.entities[1]), "no fog: enemy on explored ground shows");
    shroud.set_fog_of_war(true);
    require(!shroud.entity_visible(1, world.entities[1]), "fog: enemy out of sight hides");
    shroud.set_fog_of_war(false);

    // Dead and unplaced objects never show.
    world.entities[1].lifetime = Lifetime::Destroyed;
    require(!shroud.entity_visible(1, world.entities[1]), "destroyed enemy hides");
    world.entities[1].lifetime = Lifetime::Placed;

    // The filter and the drawing state agree with entity_visible.
    const auto filter = shroud.visible_filter(1);
    require(filter(world.entities[1]) && !filter(world.entities[2]), "filter matches");
    sim::Snapshot drawing;
    for (const auto& entity : world.entities) {
        sim::Entity drawn;
        drawn.id = entity.id;
        drawing.entities.push_back(drawn);
    }
    shroud.hide_entities(drawing, world, 1);
    require(drawing.entities.size() == 3 && drawing.entities[0].id == 1 &&
        drawing.entities[1].id == 2 && drawing.entities[2].id == 4,
        "hidden entities leave the drawing state");
}

void footprints() {
    Snapshot world;
    world.houses = {house(1), house(2)};
    world.entities = {actor(1, 1, Kind::Structure, "YARD", 20, 20),
        actor(2, 2, Kind::Structure, "YARD", 30, 20)};
    BattleShroud shroud;
    // A 4x4 building box: centre (21,21) after rounding down.
    shroud.set_footprint([](const Entity& entity) {
        std::vector<ra2map::Cell> cells;
        if (entity.kind != Kind::Structure) return cells;
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) cells.push_back(cell(entity.cell->x + x, entity.cell->y + y));
        return cells;
    });
    shroud.update(world, rules_for_test());
    require(shroud.revealed(1, cell(24, 21)), "building sees from its centre");
    require(!shroud.revealed(1, cell(17, 20)), "not from its anchor corner");
    require(!shroud.entity_visible(1, world.entities[1]), "far enemy building hidden");
    world.entities[1].cell = cell(24, 18);
    shroud.update(world, rules_for_test());
    require(!shroud.revealed(1, cell(24, 18)), "enemy anchor cell stays black");
    require(shroud.entity_visible(1, world.entities[1]),
        "enemy building shows when one footprint cell is explored");
}

// Flat test projection: cell centre at ((x-y)*30+500, (x+y)*15-1100).
std::optional<BattleShroud::FramePoint> project(ra2map::Cell where) {
    return BattleShroud::FramePoint{(int(where.x) - int(where.y)) * 30 + 500,
        (int(where.x) + int(where.y)) * 15 - 1100};
}
const std::uint8_t* at(const render::Bitmap& image, int x, int y) {
    return &image.rgba[(std::size_t(y) * image.width + x) * 4];
}

void scene_mask() {
    const auto world = battle();
    BattleShroud shroud;
    shroud.update(world, rules_for_test());
    render::Bitmap frame{1024, 768, render::Bytes(1024u * 768u * 4u, 200)};
    const auto clip = render::Rectangle{0, 0, 800, 768};
    shroud.mask_scene(frame, world, 1, project, 0, 0, clip);
    const auto centre = *project(cell(50, 50));
    require(at(frame, centre.x, centre.y)[0] == 200, "deep explored cell stays clear");
    const auto black = *project(cell(62, 56));
    require(at(frame, black.x, black.y)[0] == 0, "unexplored cell turns black");
    require(at(frame, black.x, black.y)[3] == 200, "alpha stays");
    // The edge fades: the last explored cell is partly dark, not black.
    const auto edge = *project(cell(54, 50));
    const auto value = at(frame, edge.x, edge.y)[0];
    require(value > 0 && value < 200, "edge cell fades");
    // Pixels outside the clip never change.
    const auto outside = *project(cell(40, 65));
    require(outside.x < 0 || outside.x >= 800 || at(frame, outside.x, outside.y)[0] == 0,
        "projection sanity");
    for (int y = 0; y < 768; ++y)
        for (int x = 800; x < 1024; ++x)
            if (at(frame, x, y)[0] != 200) throw std::runtime_error("mask crossed the clip");
}

// The cached mask matches a cold mask, rebuilds only on change, and follows
// the camera and newly explored cells.
render::Bitmap masked(const BattleShroud& shroud, const Snapshot& world,
                      std::int64_t camera_x, std::int64_t camera_y) {
    render::Bitmap frame{1024, 768, render::Bytes(1024u * 768u * 4u, 0)};
    for (std::size_t i = 0; i < frame.rgba.size(); ++i)
        frame.rgba[i] = static_cast<std::uint8_t>(40 + i * 7 % 200);
    shroud.mask_scene(frame, world, 1, project, camera_x, camera_y, {0, 0, 800, 768});
    return frame;
}
void mask_cache() {
    auto world = battle();
    const auto rules = rules_for_test();
    BattleShroud warm;
    warm.update(world, rules);
    const auto first = masked(warm, world, 0, 0);
    require(warm.mask_rebuilds() == 1, "first mask builds once");
    require(masked(warm, world, 0, 0).rgba == first.rgba, "warm mask equals cold mask");
    require(warm.mask_rebuilds() == 1, "unchanged view reuses the mask");
    // Camera move: same pixels as a fresh object at that camera.
    const auto moved = masked(warm, world, 90, -45);
    require(warm.mask_rebuilds() == 2, "camera move rebuilds");
    BattleShroud cold;
    cold.update(world, rules);
    require(masked(cold, world, 90, -45).rgba == moved.rgba, "moved mask equals cold mask");
    const auto centre = *project(cell(50, 50));
    const auto index = (std::size_t(centre.y + 45) * 1024 + std::size_t(centre.x - 90)) * 4;
    require(at(moved, centre.x - 90, centre.y + 45)[0] == std::uint8_t(40 + index * 7 % 200),
        "explored cell stays clear after the camera move");
    const auto black = *project(cell(62, 56));
    require(at(moved, black.x - 90, black.y + 45)[0] == 0, "black cell follows the camera");
    // New explored ground: rebuild and clear the new cells.
    world.entities[0].cell = cell(58, 50);
    warm.update(world, rules);
    const auto scouted = masked(warm, world, 0, 0);
    require(warm.mask_rebuilds() == 3, "new explored ground rebuilds");
    BattleShroud fresh;
    fresh.update(battle(), rules);
    world.entities[0].cell = cell(50, 50);
    fresh.update(world, rules);
    world.entities[0].cell = cell(58, 50);
    fresh.update(world, rules);
    require(masked(fresh, world, 0, 0).rgba == scouted.rgba, "scouted mask equals cold mask");
    const auto ground = *project(cell(60, 50));
    require(at(scouted, ground.x, ground.y)[0] != 0, "scouted cell is no longer black");
    // Same explored cells, other frame content: no rebuild.
    warm.update(world, rules);
    masked(warm, world, 0, 0);
    require(warm.mask_rebuilds() == 3, "same explored cells reuse the mask");
}

void radar_mask() {
    const auto world = battle();
    BattleShroud shroud;
    shroud.update(world, rules_for_test());
    render::RadarView view;
    view.map.size = {0, 0, 60, 120};
    view.map.local = {0, 0, 120, 120};
    view.width = 120;
    view.height = 120;
    render::Bitmap radar{120, 120, render::Bytes(120u * 120u * 4u, 90)};
    shroud.mask_radar(radar, view, 1);
    // Same lookup as the radar: pixel (px,py) shows cell (u+t, t-u+60).
    auto cell_of = [&](int px, int py) {
        const double u = px + .5, t = py + .5;
        return cell(int(std::floor(u + t + .5)), int(std::floor(t - u + 60 + .5)));
    };
    int clear = 0, dark = 0;
    for (int py = 0; py < 120; ++py)
        for (int px = 0; px < 120; ++px) {
            const auto where = cell_of(px, py);
            const bool shown = shroud.revealed(1, where);
            const auto* pixel = at(radar, px, py);
            if (shown) {
                require(pixel[0] == 90, "explored radar pixel changed");
                ++clear;
            } else {
                require(pixel[0] == 0 && pixel[3] == 255, "unexplored radar pixel not black");
                ++dark;
            }
        }
    require(clear > 0 && dark > clear, "radar shows a small explored area");
    // A radar image of another size (radar off) stays untouched.
    render::Bitmap off;
    shroud.mask_radar(off, view, 1);
    require(off.rgba.empty(), "empty radar stays empty");
}

void real_rules(const char* game) {
    fs::MixFileSystem files(game);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Installed base RULES.INI is missing");
    rules::IniFile rules;
    rules.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    // Every Allied and Soviet opening unit sees; the MCV reveals its start.
    const sim::PrototypeShroud probe(1, 1);
    for (const char* type : {"AMCV", "SMCV", "MTNK", "HTNK", "E1", "E2", "GACNST", "NACNST"})
        require(probe.sight_range(rules, type) > 0, "an opening type has no Sight");
    Snapshot world;
    world.houses = {house(1), house(2)};
    world.entities = {actor(1, 1, Kind::Unit, "AMCV", 100, 100),
        actor(2, 1, Kind::Infantry, "E1", 104, 100),
        actor(3, 2, Kind::Unit, "HTNK", 140, 140)};
    BattleShroud shroud;
    shroud.update(world, rules);
    const auto mcv = probe.sight_range(rules, "AMCV");
    require(shroud.revealed(1, cell(100 + mcv, 100)), "MCV reveals its Sight edge");
    require(!shroud.revealed(1, cell(100 - mcv - 1, 100)), "MCV sight stops");
    require(!shroud.entity_visible(1, world.entities[2]), "far Rhino stays hidden");
    std::cout << "Base RULES.INI opening Sight values verified (AMCV Sight=" << mcv << ").\n";
}
} // namespace

int main(int argc, char** argv) try {
    visibility();
    footprints();
    scene_mask();
    mask_cache();
    radar_mask();
    if (argc == 2) real_rules(argv[1]);
    std::cout << "Battle shroud checks passed: visibility, fog switch, footprints, "
                 "scene mask and radar mask.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
