#pragma once
#include "minimap.h"
#include "session/world/world.h"

namespace ra2::render {
using RadarColor = std::array<std::uint8_t, 3>;
struct RadarStyle {
    // Resolved RGB values keyed by stable House ID, not selected-color index.
    std::map<std::uint32_t, RadarColor> houses;
    std::map<std::int32_t, RadarColor> resources;
};
// Read base OverlayTypes. Resource shades are provisional; terrain uses TMP RGB.
RadarStyle battle_radar_style(const rules::IniFile& rules);
struct RadarView {
    MapGeometry map;
    int width = 160, height = 120;
    // Camera rectangle in flat map coordinates (LocalSize coordinate space).
    // Use tactical pixel bounds / 60 horizontally and / 30 vertically.
    Rectangle camera;
};
// Returns an empty image when radar_powered is false. Caller supplies the
// working Radar-building AND power check for the viewing House.
Bitmap draw_battle_radar(const session::world::Snapshot& world, TheaterArt& art,
    const RadarView& view, const RadarStyle& style, bool radar_powered);
// Radar-local pixel coordinates. Rejects missing cells, margins and power loss.
std::optional<ra2map::Cell> click_battle_radar(const session::world::Snapshot& world,
    const RadarView& view, int x, int y, bool radar_powered);
}
