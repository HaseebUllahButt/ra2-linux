#include "terrain_inputs.h"
#include "../sim/floor.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: terrain_inputs_test <game-dir> <map>");
        const auto ini = ra2::rules::IniFile::from_file(argv[2]);
        const auto terrain = ra2::pack::load_terrain(ini);
        ra2::fs::MixFileSystem files(argv[1]);
        files.bootstrap();
        auto art = ra2::render::TheaterArt::load(files, ini.read_string("Map", "Theater"));
        const auto inputs = ra2::app::terrain_inputs(terrain, art);
        if (inputs.size() != terrain.tiles.size() || inputs.empty())
            throw std::runtime_error("Original cell inputs were lost");
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            const auto& raw = terrain.tiles[i];
            if (input.cell.x != raw.x || input.cell.y != raw.y ||
                input.tile_index != raw.index || input.subtile != raw.subtile ||
                !input.tmp_terrain || !input.height_level || !input.ramp_index ||
                !input.floor_world_height || !input.overlay_type_index)
                throw std::runtime_error("Raw cell metadata did not reach simulation input");
            if (input.land_type || input.bridge)
                throw std::runtime_error("Unrecovered cell state was guessed");
        }
        // A one-cell fixture checks signed levels and Y-major overlay indexing.
        auto fixture = terrain;
        fixture.tiles.resize(1);
        fixture.tiles[0].height = 255;
        fixture.overlay.assign(512 * 512, 255);
        const auto& tile = fixture.tiles[0];
        fixture.overlay[std::size_t(tile.y) * 512 + tile.x] = 17;
        const auto result = ra2::app::terrain_inputs(fixture, art);
        if (result[0].height_level != -1 || result[0].overlay_type_index != 17)
            throw std::runtime_error("Signed height or overlay cell indexing changed");
        fixture.overlay.clear();
        if (ra2::app::terrain_inputs(fixture, art)[0].overlay_type_index != -1)
            throw std::runtime_error("Missing overlay did not use the original empty value");
        std::cout << "Native terrain inputs passed: " << inputs.size()
                  << " cells, signed levels, floor heights, and original overlay indexing.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
