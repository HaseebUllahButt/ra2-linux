#pragma once
#include "../map/map_file.h"
#include <optional>
#include <vector>
namespace ra2::sim {
// Read-only decoded map inputs. Unknown values stay empty and block dependent commands.
// Original cell fields: LandType +EC, signed height +11B, bridge flag +140 bit 100,
// and overlay-type index +44. Ramp/floor values need the decoded TMP evidence.
struct TerrainCell {
    ra2map::Cell cell;
    std::uint32_t tile_index=0;
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
}

namespace ra2::sim {
// 00544BE0's raw TMP conversion. This does not apply overlay or cliff decisions.
int original_tmp_land_type(std::uint8_t raw);
// Supported 0047D2B0 slice: valid TMP input, no overlay, and cliff-back height tests.
// Missing required neighbors or overlay metadata leave final fields empty.
void resolve_overlay_free_terrain(TerrainCells&,std::uint8_t cliff_back_impassability);
}
