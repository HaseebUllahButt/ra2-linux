#include "battle_radar.h"
#include <algorithm>
#include <cmath>

namespace ra2::render {
namespace {
bool valid(const RadarView& v) {
    const auto& l = v.map.local;
    return v.width > 0 && v.height > 0 && v.width <= 2048 && v.height <= 2048 &&
        v.map.size.width > 0 && v.map.size.width <= 512 &&
        v.map.size.height > 0 && v.map.size.height <= 512 &&
        l.x >= 0 && l.x <= 512 && l.y >= 0 && l.y <= 512 &&
        l.width > 0 && l.width <= 512 && l.height > 0 && l.height <= 512 &&
        l.x + l.width <= v.map.size.width && l.y + l.height <= v.map.size.height;
}
std::optional<ra2map::Cell> cell_at(const session::world::Snapshot& w,
    const RadarView& v, int px, int py) {
    const auto& l = v.map.local;
    const double u = l.x + (px + .5) * l.width / v.width;
    const double t = l.y + (py + .5) * l.height / v.height;
    // Flat form of original 006D1F10 projection; no elevation on radar.
    const int x = int(std::floor(u + t + .5));
    const int y = int(std::floor(t - u + v.map.size.width + .5));
    if (x < 0 || y < 0 || x >= 512 || y >= 512) return {};
    const auto found = w.cells.find(std::uint32_t(y * 512 + x));
    if (found == w.cells.end() || found->second.allocated == false ||
        !found->second.terrain) return {};
    return ra2map::Cell{std::uint16_t(x), std::uint16_t(y)};
}
void pixel(Bitmap& b, int x, int y, RadarColor c) {
    if (x < 0 || y < 0 || x >= b.width || y >= b.height) return;
    const auto i = (std::size_t(y) * b.width + x) * 4;
    std::copy(c.begin(), c.end(), b.rgba.begin() + i);
    b.rgba[i + 3] = 255;
}
}
RadarStyle battle_radar_style(const rules::IniFile& rules) {
    RadarStyle result;
    const auto* types = rules.find_section("OverlayTypes");
    if (!types) return result;
    for (const auto& entry : types->entries) {
        const auto id = rules::IniFile::integer_value(entry.key);
        if (!id || !rules.read_bool(entry.value, "Tiberium", false)) continue;
        // TODO-ORIGINAL TiberiumClass/RadarClass: recover exact resource shades.
        const bool gems = entry.value.find("GEM") != std::string::npos;
        result.resources[*id] = gems ? RadarColor{160, 80, 220} : RadarColor{220, 180, 40};
    }
    return result;
}
std::optional<ra2map::Cell> click_battle_radar(const session::world::Snapshot& w,
    const RadarView& v, int x, int y, bool powered) {
    if (!powered || !valid(v) || x < 0 || y < 0 || x >= v.width || y >= v.height) return {};
    return cell_at(w, v, x, y);
}
Bitmap draw_battle_radar(const session::world::Snapshot& w, TheaterArt& art,
    const RadarView& v, const RadarStyle& style, bool powered) {
    if (!powered) return {};
    if (!valid(v)) throw RenderError("Invalid battle radar bounds");
    Bitmap image{v.width, v.height, Bytes(std::size_t(v.width) * v.height * 4, 0)};
    for (int y = 0; y < v.height; ++y) for (int x = 0; x < v.width; ++x) {
        pixel(image, x, y, {0, 0, 0});
        const auto cell = cell_at(w, v, x, y);
        if (!cell) continue;
        const auto& state = w.cells.at(cell->y * 512u + cell->x);
        const auto& t = *state.terrain;
        const auto& meta = t.resolved_tile_index ? art.resolved_metadata(*t.resolved_tile_index, t.subtile)
                                               : art.metadata(t.tile_index, t.subtile);
        const double u = v.map.local.x + (x + .5) * v.map.local.width / v.width;
        const double center = (int(cell->x) - int(cell->y) + v.map.size.width) / 2.;
        auto color = u < center ? meta.radar_left : meta.radar_right;
        if (t.overlay_type_index) {
            const auto r = style.resources.find(*t.overlay_type_index);
            if (r != style.resources.end()) color = r->second;
        }
        pixel(image, x, y, color);
    }
    // TODO-ORIGINAL RadarClass: shroud, cloak, building foundations and marker sizes.
    // First playable match draws placed actors as single dots, buildings as 3x3.
    for (const auto& e : w.entities) {
        if (e.kind == session::world::Kind::Terrain ||
            e.lifetime != session::world::Lifetime::Placed || !e.cell ||
            e.on_map == false || e.in_limbo == true || e.active == false ||
            (e.resolved_hp && *e.resolved_hp <= 0)) continue;
        const auto color = style.houses.find(e.house_id);
        if (color == style.houses.end()) continue;
        const double u = (int(e.cell->x) - int(e.cell->y) + v.map.size.width) / 2.;
        const double t = (int(e.cell->x) + int(e.cell->y) - v.map.size.width) / 2.;
        const int x = int(std::floor((u - v.map.local.x) * v.width / v.map.local.width));
        const int y = int(std::floor((t - v.map.local.y) * v.height / v.map.local.height));
        if (u < v.map.local.x || t < v.map.local.y ||
            u >= v.map.local.x + v.map.local.width || t >= v.map.local.y + v.map.local.height) continue;
        const int r = e.kind == session::world::Kind::Structure ? 1 : 0;
        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx)
            pixel(image, x + dx, y + dy, color->second);
    }
    const auto& c = v.camera;
    if (c.width > 0 && c.height > 0) {
        const auto& l = v.map.local;
        const auto scale = [](int n, int origin, int size, int pixels) {
            return int(std::clamp((double(n) - origin) * pixels / size, -4096., 4096.));
        };
        const int x0 = scale(c.x, l.x, l.width, v.width);
        const int y0 = scale(c.y, l.y, l.height, v.height);
        const int x1 = scale(int(std::min<std::int64_t>(std::int64_t(c.x)+c.width, 2147483647)), l.x, l.width, v.width)-1;
        const int y1 = scale(int(std::min<std::int64_t>(std::int64_t(c.y)+c.height, 2147483647)), l.y, l.height, v.height)-1;
        for (int x = std::max(0,x0); x <= std::min(v.width-1,x1); ++x) {
            pixel(image,x,y0,{255,255,255}); pixel(image,x,y1,{255,255,255});
        }
        for (int y = std::max(0,y0); y <= std::min(v.height-1,y1); ++y) {
            pixel(image,x0,y,{255,255,255}); pixel(image,x1,y,{255,255,255});
        }
    }
    return image;
}
}
