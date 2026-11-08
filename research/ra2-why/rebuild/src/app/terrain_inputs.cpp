#include "terrain_inputs.h"
#include "../sim/floor.h"
#include <map>
#include <stdexcept>

namespace ra2::app {
namespace {
std::optional<sim::TileMetadata> normalized_metadata(render::TheaterArt& art,
                                                   std::uint32_t index, std::uint8_t subtile) {
    try {
        const auto count = art.resolved_subtile_count(index);
        if (!count) return std::nullopt;
        const bool valid = art.resolved_subtile_present(index, subtile);
        const auto normalized = static_cast<std::uint8_t>(subtile % count);
        if (!art.resolved_subtile_present(index, normalized)) return sim::TileMetadata{0,0,false};
        const auto& record = art.resolved_metadata(index, normalized);
        return sim::TileMetadata{record.terrain, record.ramp, valid};
    } catch (const render::RenderError&) { return std::nullopt; }
}
}
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
        else if (const auto metadata = normalized_metadata(art, *input.resolved_tile_index, tile.subtile)) {
            input.tmp_terrain = metadata->terrain;
            input.ramp_index = metadata->ramp;
        }
        // The original cell +11B is signed, although the map record is a byte.
        const int level = tile.height < 128 ? int(tile.height) : int(tile.height) - 256;
        input.height_level = level;
        // This sample is at the center. Other subcell positions must call the
        // recovered floor function with their actual original world X and Y.
        if (input.ramp_index)
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
        return normalized_metadata(art, index, subtile);
    };
    return result;
}

pack::Terrain initialized_terrain(const pack::Terrain& source, const sim::TerrainCells& cells) {
    std::map<std::pair<std::uint16_t,std::uint16_t>, const sim::TerrainCell*> by_cell;
    for (const auto& cell : cells) {
        if (!cell.tile_replacement_resolved || !cell.resolved_tile_index || !cell.height_level ||
            *cell.height_level < -128 || *cell.height_level > 127)
            throw std::runtime_error("Terrain drawing needs completed original tile initialization");
        if (!by_cell.emplace(std::make_pair(cell.cell.x, cell.cell.y), &cell).second)
            throw std::runtime_error("Duplicate initialized terrain cell");
    }
    if (source.tiles.size() != cells.size())
        throw std::runtime_error("Initialized terrain does not match the packed map");
    auto result = source;
    for (auto& tile : result.tiles) {
        const auto found = by_cell.find({tile.x, tile.y});
        if (found == by_cell.end()) throw std::runtime_error("Initialized terrain cell is missing");
        const auto& cell = *found->second;
        tile.index = *cell.resolved_tile_index;
        tile.subtile = cell.subtile;
        tile.height = static_cast<std::uint8_t>(*cell.height_level);
    }
    return result;
}
}
