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
        sim::TerrainCell input;
        input.cell = {tile.x, tile.y};
        input.tile_index = tile.index;
        input.subtile = tile.subtile;
        input.resolved_tile_index = art.translate_map_index(tile.index);
        if (*input.resolved_tile_index == 0xffff) input.ramp_index = 0;
        else {
            const auto& metadata = art.resolved_metadata(*input.resolved_tile_index, tile.subtile);
            input.tmp_terrain = metadata.terrain;
            input.ramp_index = metadata.ramp;
        }
        // The original cell +11B is signed, although the map record is a byte.
        const int level = tile.height < 128 ? int(tile.height) : int(tile.height) - 256;
        input.height_level = level;
        // This sample is at the center. Other subcell positions must call the
        // recovered floor function with their actual original world X and Y.
        input.floor_world_height = sim::original_floor_height(static_cast<std::int8_t>(level),
            static_cast<std::uint8_t>(*input.ramp_index), int(tile.x) * 256 + 128, int(tile.y) * 256 + 128);
        const auto index = std::size_t(tile.y) * 512 + tile.x;
        const auto overlay = terrain.overlay.empty() ? 255 : terrain.overlay[index];
        input.overlay_type_index = overlay == 255 ? -1 : int(overlay);
        // 0047D2B0 can change LandType after the raw TMP lookup. Bridge flags
        // also depend on original map initialization. Unknown is not false.
        out.push_back(input);
    }
    return out;
}

sim::TheaterInputs theater_inputs(render::TheaterArt& art) {
    sim::TheaterInputs result;
    result.theater_name = art.theater_name();
    result.settings = art.settings();
    for (const auto& set : art.tile_sets()) result.tile_sets.push_back({set.first, set.count});
    result.resolved_metadata = [&art](std::uint32_t index, std::uint8_t subtile)
        -> std::optional<sim::TileMetadata> {
        try {
            const auto& record = art.resolved_metadata(index, subtile);
            return sim::TileMetadata{record.terrain, record.ramp};
        } catch (const render::RenderError&) { return std::nullopt; }
    };
    return result;
}
}
