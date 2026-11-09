#include "battle_shroud.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ra2::app {
namespace {
using session::world::Entity;
using session::world::Lifetime;

bool placed(const Entity& entity) {
    return entity.lifetime == Lifetime::Placed && entity.cell.has_value() &&
        entity.on_map.value_or(true);
}

ra2map::Cell to_cell(std::int32_t x, std::int32_t y) {
    return ra2map::Cell{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
}
} // namespace

BattleShroud::BattleShroud() : shroud_(grid_size, grid_size) {}

std::vector<ra2map::Cell> BattleShroud::cells_of(const Entity& entity) const {
    std::vector<ra2map::Cell> cells;
    if (footprint_) cells = footprint_(entity);
    if (cells.empty() && entity.cell) cells.push_back(*entity.cell);
    return cells;
}

void BattleShroud::update(const session::world::Snapshot& world, const rules::IniFile& rules) {
    // 006D5C30 sights from the object coordinate. For a building that is the
    // footprint centre: the middle of its cell box, rounded down.
    shroud_.tick(world, rules, [this](const Entity& entity) -> std::optional<ra2map::Cell> {
        if (entity.kind != session::world::Kind::Structure || !footprint_) return {};
        const auto cells = footprint_(entity);
        if (cells.empty()) return {};
        std::int32_t left = cells[0].x, right = left, top = cells[0].y, bottom = top;
        for (const auto cell : cells) {
            left = std::min<std::int32_t>(left, cell.x);
            right = std::max<std::int32_t>(right, cell.x);
            top = std::min<std::int32_t>(top, cell.y);
            bottom = std::max<std::int32_t>(bottom, cell.y);
        }
        return to_cell((left + right) / 2, (top + bottom) / 2);
    });
}

bool BattleShroud::revealed(std::uint32_t house, ra2map::Cell cell) const {
    return shroud_.cell_revealed_to(house, cell);
}

bool BattleShroud::visible_now(std::uint32_t house, ra2map::Cell cell) const {
    return shroud_.cell_visible_to(house, cell);
}

bool BattleShroud::objects_shown(std::uint32_t house, ra2map::Cell cell) const {
    return fog_of_war_ ? visible_now(house, cell) : revealed(house, cell);
}

bool BattleShroud::coord_shown(std::uint32_t house, sim::Coord coord) const {
    if (coord.x < 0 || coord.y < 0) return false;
    const auto x = coord.x / 256, y = coord.y / 256;
    if (x >= grid_size || y >= grid_size) return false;
    return objects_shown(house, to_cell(x, y));
}

bool BattleShroud::entity_visible(std::uint32_t house, const Entity& entity) const {
    if (!placed(entity)) return false;
    if (house != 0 && entity.house_id == house) return true;
    // TODO-ORIGINAL: base BuildingClass::IsAllFogged 00452BE0 and cloak or
    // disguise checks are not modeled; any shown footprint cell shows it.
    for (const auto cell : cells_of(entity))
        if (objects_shown(house, cell)) return true;
    return false;
}

std::function<bool(const Entity&)> BattleShroud::visible_filter(std::uint32_t house) const {
    return [this, house](const Entity& entity) { return entity_visible(house, entity); };
}

void BattleShroud::hide_entities(sim::Snapshot& drawing, const session::world::Snapshot& world,
                                 std::uint32_t house) const {
    std::unordered_set<std::uint32_t> hidden;
    for (const auto& entity : world.entities)
        if (!entity_visible(house, entity)) hidden.insert(entity.id);
    drawing.entities.erase(std::remove_if(drawing.entities.begin(), drawing.entities.end(),
        [&](const sim::Entity& entity) { return hidden.count(entity.id) != 0; }),
        drawing.entities.end());
}

bool BattleShroud::revealed_xy(std::uint32_t house, std::int32_t x, std::int32_t y) const {
    if (x < 0 || y < 0 || x >= grid_size || y >= grid_size) return false;
    return revealed(house, to_cell(x, y));
}

int BattleShroud::corner_darkness(std::uint32_t house, ra2map::Cell cell) const {
    const std::int32_t x = cell.x, y = cell.y;
    return int(!revealed_xy(house, x - 1, y - 1)) + int(!revealed_xy(house, x, y - 1)) +
        int(!revealed_xy(house, x - 1, y)) + int(!revealed_xy(house, x, y));
}

void BattleShroud::mask_scene(render::Bitmap& frame, const session::world::Snapshot& world,
                              std::uint32_t house, const CellAnchor& anchor,
                              render::Rectangle clip) const {
    if (!anchor || frame.width <= 0 || frame.height <= 0) return;
    if (frame.rgba.size() < std::size_t(frame.width) * frame.height * 4) return;
    const int left = std::max(0, clip.x), top = std::max(0, clip.y);
    const int right = std::min(frame.width, clip.x + clip.width);
    const int bottom = std::min(frame.height, clip.y + clip.height);
    if (left >= right || top >= bottom) return;
    // One cell is a 60x30 diamond. Corner darkness is bilinear inside it, so
    // the black edge fades over about one cell, like the original soft edge.
    // TODO-ORIGINAL: the base shroud edge art (Cell_UpdateTwoShroudEdgeMasks
    // 00475520) and sloped cell shapes are not drawn; this is a flat fade.
    constexpr int full = 4 * 60 * 60;
    shroud_.revealed_layer(house, layer_);
    const auto shown = [&](std::int32_t x, std::int32_t y) {
        return x >= 0 && y >= 0 && x < grid_size && y < grid_size &&
            layer_[std::size_t(y) * grid_size + x] != 0;
    };
    const auto corner = [&](std::int32_t x, std::int32_t y) {
        return 4 - int(shown(x - 1, y - 1)) - int(shown(x, y - 1)) - int(shown(x - 1, y)) -
            int(shown(x, y));
    };
    for (const auto& [key, state] : world.cells) {
        if (!state.allocated.value_or(false)) continue;
        const std::int32_t x = std::int32_t(key % 512), y = std::int32_t(key / 512);
        const int top_corner = corner(x, y), right_corner = corner(x + 1, y);
        const int left_corner = corner(x, y + 1), bottom_corner = corner(x + 1, y + 1);
        if (top_corner + right_corner + left_corner + bottom_corner == 0) continue;
        const bool black = top_corner + right_corner + left_corner + bottom_corner == 16;
        const auto centre = anchor(to_cell(x, y));
        if (!centre) continue;
        if (centre->x + 30 < left || centre->x - 30 >= right || centre->y + 15 < top ||
            centre->y - 15 >= bottom)
            continue;
        for (int dy = -15; dy < 15; ++dy) {
            const int py = centre->y + dy;
            if (py < top || py >= bottom) continue;
            // Diamond row: s = dx+2dy+30 and t = 2dy-dx+30 both in [0,60).
            const int from = std::max({-30, -2 * dy - 30, 2 * dy - 29});
            const int to = std::min({29, 29 - 2 * dy, 2 * dy + 30});
            for (int dx = from; dx <= to; ++dx) {
                const int px = centre->x + dx;
                if (px < left || px >= right) continue;
                auto* pixel = &frame.rgba[(std::size_t(py) * frame.width + px) * 4];
                if (black) {
                    pixel[0] = pixel[1] = pixel[2] = 0;
                    continue;
                }
                // s runs along +x (towards the right corner), t along +y.
                const int s = dx + 2 * dy + 30, t = 2 * dy - dx + 30;
                const int dark = top_corner * (60 - s) * (60 - t) + right_corner * s * (60 - t) +
                    left_corner * (60 - s) * t + bottom_corner * s * t;
                if (dark <= 0) continue;
                for (int channel = 0; channel < 3; ++channel)
                    pixel[channel] = static_cast<std::uint8_t>(pixel[channel] * (full - dark) / full);
            }
        }
    }
}

void BattleShroud::mask_radar(render::Bitmap& radar, const render::RadarView& view,
                              std::uint32_t house) const {
    const auto& local = view.map.local;
    if (radar.width != view.width || radar.height != view.height || local.width <= 0 ||
        local.height <= 0 || radar.rgba.size() < std::size_t(radar.width) * radar.height * 4)
        return;
    for (int py = 0; py < radar.height; ++py) {
        for (int px = 0; px < radar.width; ++px) {
            // Same flat cell lookup as draw_battle_radar (original 006D1F10
            // projection without elevation).
            const double u = local.x + (px + .5) * local.width / view.width;
            const double t = local.y + (py + .5) * local.height / view.height;
            const int x = int(std::floor(u + t + .5));
            const int y = int(std::floor(t - u + view.map.size.width + .5));
            if (revealed_xy(house, x, y)) continue;
            auto* pixel = &radar.rgba[(std::size_t(py) * radar.width + px) * 4];
            pixel[0] = pixel[1] = pixel[2] = 0;
            pixel[3] = 255;
        }
    }
}

} // namespace ra2::app
