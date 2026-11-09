#pragma once
#include "setup.h"
namespace ra2::session::world {
struct OverlayTheaterShape {
    std::string filename;
    std::vector<std::uint8_t> bytes;
};
// Base 005DC960 preloads this checked Theater=yes, DemandLoad=no path.
// The input image name and flags must come from the original type readers.
// Missing files and all other branches remain unresolved, including the
// original fallback. The returned bytes are owned, not a live type pointer.
std::optional<OverlayTheaterShape> preload_base_overlay_theater_shape(
    const BaseBattleAssets&,const std::string& resolved_image,
    std::optional<bool> theater,std::optional<bool> demand_load,
    const std::string& resolved_theater_suffix);
}
