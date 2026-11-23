#pragma once
#include "setup.h"
namespace ra2::session::world {
struct OverlayArtFields {
    std::string image;
    bool theater=false, new_theater=false, demand_load=false;
};
// Constructor state and one original ObjectType/OverlayType reader layer.
// The caller must apply every real rules layer in its original order.
std::optional<OverlayArtFields> base_overlay_art_constructor(const std::string& type);
std::optional<OverlayArtFields> read_base_overlay_art_slice(const rules::IniFile& rules_layer,
    const rules::IniFile& art,const std::string& type,OverlayArtFields before);

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
