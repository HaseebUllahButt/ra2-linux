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
        files.bootstrap(ra2::fs::GameVariant::RedAlert2);
        auto art = ra2::render::TheaterArt::load(files, ini.read_string("Map", "Theater"));
        const auto inputs = ra2::app::terrain_inputs(terrain, art);
        if (inputs.size() != terrain.tiles.size() || inputs.empty())
            throw std::runtime_error("Original cell inputs were lost");
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const auto& input = inputs[i];
            const auto& raw = terrain.tiles[i];
            if (input.cell.x != raw.x || input.cell.y != raw.y ||
                input.tile_index != raw.index || input.subtile != raw.subtile ||
                !input.resolved_tile_index ||
                (*input.resolved_tile_index != 0xffff && !input.tmp_terrain) ||
                !input.height_level || !input.ramp_index ||
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
        const auto catalog = ra2::app::theater_inputs(art);
        if (catalog.theater_name != "TEMPERATE" || catalog.tile_sets.empty() ||
            !catalog.resolved_metadata || catalog.resolved_metadata(0xffff, 0))
            throw std::runtime_error("Original theater callback did not preserve the clear sentinel");
        const auto clear_set = static_cast<std::size_t>(catalog.settings.read_integer("General", "ClearTile", 0));
        const auto live_clear = catalog.tile_sets.at(clear_set).first;
        const auto subtile_count = art.resolved_subtile_count(live_clear);
        if (!subtile_count || subtile_count > 255)
            throw std::runtime_error("Original clear TMP no longer supplies the modulo fixture");
        const auto direct = catalog.resolved_metadata(live_clear, 0);
        const auto wrapped = catalog.resolved_metadata(live_clear, static_cast<std::uint8_t>(subtile_count));
        if (!direct || !wrapped || !direct->valid_subtile || wrapped->valid_subtile ||
            direct->terrain != wrapped->terrain || direct->ramp != wrapped->ramp)
            throw std::runtime_error("Modulo TMP getters changed raw subtile validation");
        fixture.tiles[0].index = 0x7fffffff;
        fixture.tiles[0].subtile = 0;
        const auto invalid = ra2::app::terrain_inputs(fixture, art).front();
        if (invalid.tile_index != 0x7fffffff || !invalid.resolved_tile_index ||
            invalid.tmp_terrain || invalid.ramp_index || invalid.floor_world_height ||
            catalog.resolved_metadata(*invalid.resolved_tile_index, 0))
            throw std::runtime_error("Invalid source tile did not reach original initialization");
        fixture.tiles[0].index = 0;
        fixture.tiles[0].subtile = 255;
        const auto invalid_subtile = ra2::app::terrain_inputs(fixture, art).front();
        if (invalid_subtile.subtile != 255 || !invalid_subtile.resolved_tile_index)
            throw std::runtime_error("Invalid source subtile was changed before initialization");
        fixture.tiles[0].index = 0xffff;
        fixture.tiles[0].subtile = 99; // No TMP lookup is valid for the sentinel.
        const auto clear = ra2::app::terrain_inputs(fixture, art).front();
        if (clear.tile_index != 0xffff || clear.resolved_tile_index != 0xffff ||
            clear.ramp_index != 0 || clear.tmp_terrain)
            throw std::runtime_error("Clear land was replaced by renderer fallback metadata");
        // The original loader permits sparse records, repeated cells, and records
        // outside the allocated diamond. Check constructor defaults and last writes.
        ra2::pack::Terrain sparse;
        sparse.overlay.assign(512*512, 255);
        sparse.overlay[3*512+1] = 17;
        sparse.tiles = {{1,3,12,1,4,3}, {1,3,13,2,5,7}, {0,1,999,0,0,0}};
        const auto allocated = ra2::app::allocated_terrain(sparse, {{0,0,3,2},{0,0,3,2}});
        if (allocated.tiles.size() != 10 || allocated.overlay != sparse.overlay ||
            allocated.tiles.front().index != 13 || allocated.tiles.front().subtile != 2 ||
            allocated.tiles.front().height != 5 || allocated.tiles.front().ice != 7)
            throw std::runtime_error("Sparse map loading lost defaults or ordered record writes");
        for (std::size_t i = 1; i < allocated.tiles.size(); ++i)
            if (allocated.tiles[i].index != 0xffff || allocated.tiles[i].subtile ||
                allocated.tiles[i].height || allocated.tiles[i].ice)
                throw std::runtime_error("Missing map records lost original Cell constructor defaults");
        std::cout << "Native terrain inputs passed: " << inputs.size()
                  << " cells, signed levels, floor heights, and original overlay indexing.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
