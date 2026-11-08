#include "scene.h"
#include "terrain_inputs.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
ra2::rules::IniFile read_ini(const ra2::fs::MixFileSystem& files, const char* name) {
    const auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing original ") + name);
    ra2::rules::IniFile result;
    result.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    return result;
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: base_move_test <game-dir>");
    using namespace ra2;
    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    const auto map = read_ini(files, "ALL02S.MAP");
    const auto rules = read_ini(files, "RULES.INI");
    const auto art = read_ini(files, "ART.INI");
    const auto packed = app::allocated_terrain(pack::load_terrain(map), render::MapGeometry::from_ini(map));
    auto tiles = render::TheaterArt::load(files, map.read_string("Map", "Theater"));
    const auto cells = app::terrain_inputs(packed, tiles);
    const auto theater = app::theater_inputs(tiles);
    sim::Simulation first(rules, map, 9, cells, &theater), replay(rules, map, 9, cells, &theater);
    const auto executable = (std::filesystem::path(argv[1]) / "game.exe").string();
    first.load_ground_assets(executable, art);
    replay.load_ground_assets(executable, art);
    constexpr std::uint32_t id = 60;
    const auto initial = first.snapshot().entities.at(id-1);
    require(initial.type == "HTNK" && initial.cell.x == 114 && initial.cell.y == 67 &&
            initial.facing == 128 && initial.position && first.types().at("HTNK").speed == 6,
            "Original base campaign tank fixture changed");
    const auto geometry = render::MapGeometry::from_ini(map);
    app::Scene scene(files, art, app::overlay_rules(rules, map), geometry, first.terrain(), "SNOW");
    const auto live = app::initialized_terrain(packed, first.terrain());
    const auto battlefield = render::render_minimap(live, geometry, tiles, render::TileIndexSpace::LiveCell);
    const auto start_anchor = scene.map_anchor(initial);
    const render::Rectangle viewport{static_cast<int>(start_anchor.x)-160,
                                     static_cast<int>(start_anchor.y)-120, 320, 240};
    sim::Snapshot isolated;
    isolated.entities = {initial};
    const auto start_frame = scene.render(battlefield, isolated, viewport);
    require(start_frame.drawn == 1 && start_frame.gaps.empty(), "Base campaign tank did not draw");
    sim::Command move;
    move.op = sim::Op::Move; move.entity = id; move.destination = {114,68};
    require(first.issue(move).error == sim::Error::None && replay.issue(move).error == sim::Error::None,
            "Base campaign Move did not queue");
    first.tick(); replay.tick();
    require(first.snapshot().results.at(0).error == sim::Error::None &&
            replay.snapshot().results.at(0).error == sim::Error::None,
            "Base campaign Move was rejected");
    // Original base 004A1000, facing 128, resolved Speed 15. The x86
    // evidence harness original_move_trace.py supplies these displacements.
    constexpr int original_y[] = {23,47,70,94,117,141,164,188,212,235,253,256};
    for (int step = 0; step < 12; ++step) {
        first.tick(); replay.tick();
        const auto actual = first.snapshot().entities.at(id-1);
        const auto repeated = replay.snapshot().entities.at(id-1);
        require(actual.position && repeated.position &&
                actual.position->x == initial.position->x &&
                actual.position->y == initial.position->y + original_y[step] &&
                actual.position->z == initial.position->z,
                "Base campaign Move differs from original x86 positions");
        require(actual.position->x == repeated.position->x && actual.position->y == repeated.position->y &&
                actual.position->z == repeated.position->z && actual.moving == repeated.moving &&
                actual.mission == repeated.mission,
                "Base campaign Move replay differs");
        require(actual.moving == (step < 11) && actual.hp == initial.hp && actual.house == initial.house,
                "Base campaign Move changed arrival, health, or owner");
        isolated.entities = {actual};
        const auto frame = scene.render(battlefield, isolated, viewport);
        require(frame.drawn == 1 && frame.gaps.empty(), "Moving base tank left the native scene");
    }
    const auto final = first.snapshot().entities.at(id-1);
    const auto end_anchor = scene.map_anchor(final);
    require(final.cell.x == 114 && final.cell.y == 68 &&
            end_anchor.x == start_anchor.x - 30 && end_anchor.y == start_anchor.y + 15,
            "Moving tank lost its original world-to-screen conversion");
    require(scene.render(battlefield, isolated, viewport).image.rgba != start_frame.image.rgba,
            "Moving tank kept its old screen pixels");
    move.frame = first.snapshot().frame;
    move.destination = {114,70};
    require(first.issue(move).error == sim::Error::None, "Unsupported route did not reach validation");
    first.tick();
    require(first.snapshot().results.at(0).error == sim::Error::Unsupported &&
            first.snapshot().entities.at(id-1).position->y == final.position->y,
            "Unsupported route changed the base campaign tank position");
    std::cout << "Base campaign Move passed: ALL02S HTNK 114,67 -> 114,68, 12 original updates and native scene frames.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
