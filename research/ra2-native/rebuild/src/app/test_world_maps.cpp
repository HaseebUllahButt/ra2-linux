#include "terrain_inputs.h"
#include "../sim/sim.h"
#include "../ui/ui.h"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
ra2::rules::IniFile read_ini(const ra2::fs::MixFileSystem& files, const std::string& name) {
    const auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error("Missing original " + name);
    ra2::rules::IniFile result;
    result.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    return result;
}
void prepare(ra2::fs::MixFileSystem& files, ra2::fs::GameVariant variant) {
    files.mount_language(variant);
    files.bootstrap(variant);
    files.mount_overlays();
    files.mount_resources();
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: world_maps_test <game-dir>");
    using namespace ra2;
    for (const auto variant : {fs::GameVariant::RedAlert2, fs::GameVariant::YurisRevenge}) {
        const bool expansion = variant == fs::GameVariant::YurisRevenge;
        fs::MixFileSystem catalog(argv[1]);
        prepare(catalog, variant);
        const auto csf = catalog.read_file(expansion ? "RA2MD.CSF" : "RA2.CSF");
        if (!csf) throw std::runtime_error("Original map names are missing");
        ra2text::CsfFile strings;
        strings.parse(csf->data(), csf->size());
        std::map<std::string, std::vector<std::string>> groups;
        std::size_t unavailable = 0;
        for (const auto& map : ui::load_skirmish_maps(catalog, strings)) {
            if (!map.built_in) continue;
            if (!map.available) {
                ++unavailable;
                std::cout << (expansion ? "Yuri" : "Base") << " catalog asset unavailable: " << map.filename << '\n';
                continue;
            }
            const auto ini = read_ini(catalog, map.filename);
            groups[ini.read_string("Map", "Theater")].push_back(map.filename);
        }
        std::size_t maps = 0, cells = 0, changed = 0, objects = 0;
        for (const auto& [theater_name, names] : groups) {
            fs::MixFileSystem files(argv[1]);
            prepare(files, variant);
            auto art = render::TheaterArt::load(files, theater_name);
            const auto theater = app::theater_inputs(art);
            const auto rules = read_ini(files, expansion ? "RULESMD.INI" : "RULES.INI");
            for (const auto& name : names) try {
                const auto map = read_ini(files, name);
                const auto packed = app::allocated_terrain(pack::load_terrain(map), render::MapGeometry::from_ini(map));
                const auto inputs = app::terrain_inputs(packed, art);
                sim::Simulation simulation(rules, map, 0, inputs, &theater);
                const auto live = app::initialized_terrain(packed, simulation.terrain());
                for (std::size_t i = 0; i < live.tiles.size(); ++i) {
                    const auto& cell = simulation.terrain().at(i);
                    if (!cell.tile_replacement_resolved || !cell.floor_world_height || !cell.ramp_index)
                        throw std::runtime_error("Original ordered setup left a floor unresolved");
                    changed += cell.resolved_tile_index != inputs[i].resolved_tile_index ||
                               cell.subtile != inputs[i].subtile;
                }
                objects += simulation.snapshot().entities.size();
                cells += live.tiles.size();
                ++maps;
            } catch (const std::exception& error) {
                throw std::runtime_error(name + ": " + error.what());
            }
            std::cout << (expansion ? "Yuri " : "Base ") << theater_name << ": " << names.size()
                      << " maps initialized.\n" << std::flush;
        }
        if (maps + unavailable != (expansion ? 161u : 137u) || !changed)
            throw std::runtime_error("Original map catalog or live tile replacement coverage changed");
        std::cout << (expansion ? "Yuri" : "Base") << " world audit passed: " << maps << " maps, "
                  << cells << " cells, " << changed << " replaced tiles, " << objects << " objects.\n";
        std::cout << "Unavailable catalog assets: " << unavailable << ".\n";
    }
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
