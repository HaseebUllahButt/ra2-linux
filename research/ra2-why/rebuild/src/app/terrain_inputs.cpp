#include "terrain_inputs.h"
#include "../sim/floor.h"
#include <stdexcept>

namespace ra2::app {
sim::TerrainCells terrain_inputs(const pack::Terrain& terrain, render::TheaterArt& art) {
    constexpr std::size_t cells = 512 * 512;
    if (!terrain.overlay.empty() && terrain.overlay.size() != cells)
        throw std::runtime_error("Original overlay grid has the wrong size");
    sim::TerrainCells out;
    out.reserve(terrain.tiles.size());
    for (const auto& tile : terrain.tiles) {
        if (tile.x >= 512 || tile.y >= 512)
            throw std::runtime_error("Terrain cell is outside the original grid");
        const auto& metadata = art.metadata(tile.index, tile.subtile);
        sim::TerrainCell input;
        input.cell = {tile.x, tile.y};
        input.tile_index = tile.index;
        input.subtile = tile.subtile;
        input.tmp_terrain = metadata.terrain;
        input.ramp_index = metadata.ramp;
        // The original cell +11B is signed, although the map record is a byte.
        const int level = tile.height < 128 ? int(tile.height) : int(tile.height) - 256;
        input.height_level = level;
        // This sample is at the center. Other subcell positions must call the
        // recovered floor function with their actual original world X and Y.
        input.floor_world_height = sim::original_floor_height(static_cast<std::int8_t>(level),
            metadata.ramp, int(tile.x) * 256 + 128, int(tile.y) * 256 + 128);
        const auto index = std::size_t(tile.y) * 512 + tile.x;
        const auto overlay = terrain.overlay.empty() ? 255 : terrain.overlay[index];
        input.overlay_type_index = overlay == 255 ? -1 : int(overlay);
        // 0047D2B0 can change LandType after the raw TMP lookup. Bridge flags
        // also depend on original map initialization. Unknown is not false.
        out.push_back(input);
    }
    return out;
}
}
