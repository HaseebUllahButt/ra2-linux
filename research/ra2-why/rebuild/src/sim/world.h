#pragma once
#include "../map/map_file.h"
#include <optional>
#include <functional>
#include <string>
#include <vector>
namespace ra2::sim {
// Read-only decoded map inputs. Unknown values stay empty and block dependent commands.
// Original cell fields: LandType +EC, signed height +11B, bridge flag +140 bit 100,
// and overlay-type index +44. Ramp/floor values need the decoded TMP evidence.
struct TerrainCell {
    ra2map::Cell cell;
    std::uint32_t tile_index=0; // Raw packed-map index.
    bool tile_replacement_resolved=false; // Set by the original ordered setup pass.
    std::optional<std::uint32_t> resolved_tile_index; // Live Cell +38; keep the 0xFFFF sentinel.
    std::uint8_t subtile=0;
    std::optional<std::uint8_t> tmp_terrain; // Raw TMP byte, not final Cell LandType.
    std::optional<std::int32_t> land_type;
    std::optional<std::int32_t> height_level;
    std::optional<std::int32_t> ramp_index;
    std::optional<std::int32_t> floor_world_height;
    std::optional<bool> bridge;
    std::optional<std::int32_t> overlay_type_index; // -1 means no overlay.
};
using TerrainCells=std::vector<TerrainCell>;
// Base 00470F10 / expansion 0047BBF0, then the scenario's zero initial height.
// Apply decoded overlay-grid values after creating this default cell.
TerrainCell original_default_terrain_cell(ra2map::Cell);
// 00545150 catalog order. The vector index is the original TileSet number.
struct TileSetRange { std::uint32_t first=0,count=0; };
struct TileMetadata {
    std::uint8_t terrain=0,ramp=0; // Getter values use original subtile modulo.
    bool valid_subtile=true; // 00544C20 validates the unwrapped input index separately.
};
struct TheaterInputs {
    std::string theater_name;
    rules::IniFile settings; // Original theater INI; [General] values name TileSet numbers.
    std::vector<TileSetRange> tile_sets; // Live indexes after LastTilesInSet expansion.
    // Read static original TMP data by live index. Do not apply map-index shifts again.
    // Initialization uses this callback synchronously and does not retain it.
    std::function<std::optional<TileMetadata>(std::uint32_t,std::uint8_t)> resolved_metadata;
};
}

namespace ra2::sim {
// 00544BE0's raw TMP conversion. This does not apply overlay or cliff decisions.
int original_tmp_land_type(std::uint8_t raw);
// Supported 0047D2B0 slice: valid TMP input, no overlay, and cliff-back height tests.
// Missing required neighbors or overlay metadata leave final fields empty.
void resolve_overlay_free_terrain(TerrainCells&,std::uint8_t cliff_back_impassability);
}
