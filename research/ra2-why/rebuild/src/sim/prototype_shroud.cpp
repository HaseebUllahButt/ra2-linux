#include "prototype_shroud.h"

#include <algorithm>

namespace ra2::sim {
namespace {
using session::world::Entity;
using session::world::HouseRole;
using session::world::Lifetime;

// RevealArea 0054A110 clamps the sight range down to ten cells.
constexpr std::int32_t max_sight_range = 10;

// The base CellSpread area for one range (offsets 00A6F248, counts
// 007A5908). A cell counts when max(|dx|,|dy|) + min(|dx|,|dy|)/2 <= range;
// test_prototype_shroud checks every count of the original table.
template <typename Visit>
void for_each_cell_in_diamond(ra2map::Cell center, std::int32_t range, Visit visit) {
    const auto cx = static_cast<std::int32_t>(center.x);
    const auto cy = static_cast<std::int32_t>(center.y);
    for (std::int32_t dy = -range; dy <= range; ++dy) {
        for (std::int32_t dx = -range; dx <= range; ++dx) {
            const auto across = std::max(std::abs(dx), std::abs(dy));
            const auto diagonal = std::min(std::abs(dx), std::abs(dy));
            if (across + diagonal / 2 > range) continue;
            visit(ra2map::Cell{static_cast<std::uint16_t>(cx + dx),
                               static_cast<std::uint16_t>(cy + dy)});
        }
    }
}

// A live entity that stands on the map and can see. Terrain has no house and
// never sights; Destroyed and not-yet-placed objects do not sight either.
bool sight_source(const Entity& entity) {
    return entity.lifetime == Lifetime::Placed && entity.house_id != 0 &&
        entity.cell.has_value() && entity.on_map.value_or(true);
}

// SpySat eligibility, from MapClass grant/loss checks in FUN_004F2FC0
// (BuildingType +1345 SpySat flag): the building must be alive (+74), not in
// limbo (+76), and its current mission must not be 0x13.
// resolved_hp>0 stands in for the +74 alive check when present. Reveal runs
// through 0055A120; losing the last satellite reshrouds through 00559F10.
// This path has no power test: a powered-down satellite still grants the
// full map view. TODO: the snapshot has no queued mission field, so the
// queued-mission half of the base check is unresolved.
bool satellite_eligible(const Entity& entity) {
    if (entity.resolved_hp && *entity.resolved_hp <= 0) return false;
    if (entity.active && !*entity.active) return false;
    if (entity.in_limbo && *entity.in_limbo) return false;
    if (entity.mission && *entity.mission == 0x13) return false;
    return true;
}

bool map_neutral(const session::world::House& house) {
    return house.role && (*house.role == HouseRole::Neutral || *house.role == HouseRole::Special);
}
} // namespace

PrototypeShroud::PrototypeShroud(std::int32_t width, std::int32_t height)
    : width_(width < 0 ? 0 : width), height_(height < 0 ? 0 : height) {}

bool PrototypeShroud::in_grid(ra2map::Cell cell) const noexcept {
    return cell.x < static_cast<std::uint32_t>(width_) &&
        cell.y < static_cast<std::uint32_t>(height_);
}

std::size_t PrototypeShroud::index_of(ra2map::Cell cell) const noexcept {
    return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width_) + cell.x;
}

PrototypeShroud::HouseMap& PrototypeShroud::map_for(std::uint32_t house) {
    auto& map = houses_[house];
    const auto cells = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    map.revealed.resize(cells, 0);
    map.visible.resize(cells, 0);
    map.gaps.resize(cells, 0);
    return map;
}

const PrototypeShroud::HouseMap* PrototypeShroud::find_map(std::uint32_t house) const noexcept {
    const auto found = houses_.find(house);
    return found == houses_.end() ? nullptr : &found->second;
}

void PrototypeShroud::sight_from(std::uint32_t house, ra2map::Cell center, std::int32_t range) {
    // MapClass::Sight_From rejects an off-map center with In_Radar.
    if (house == 0 || range <= 0 || !in_grid(center)) return;
    range = std::min(range, max_sight_range);
    auto& map = map_for(house);
    for_each_cell_in_diamond(center, range, [&](ra2map::Cell cell) {
        if (!in_grid(cell)) return;
        map.revealed[index_of(cell)] = 1;
        map.visible[index_of(cell)] = 1;
    });
}

void PrototypeShroud::shroud_from(std::uint32_t house, ra2map::Cell center, std::int32_t range) {
    if (house == 0 || range <= 0 || !in_grid(center)) return;
    range = std::min(range, max_sight_range);
    for (auto& [other, map] : houses_) {
        if (other == house) continue;
        for_each_cell_in_diamond(center, range, [&](ra2map::Cell cell) {
            if (!in_grid(cell)) return;
            ++map.gaps[index_of(cell)];
        });
    }
}

void PrototypeShroud::reveal_all(std::uint32_t house) {
    if (house == 0) return;
    auto& map = map_for(house);
    std::fill(map.revealed.begin(), map.revealed.end(), 1);
    std::fill(map.visible.begin(), map.visible.end(), 1);
}

void PrototypeShroud::reshroud_all(std::uint32_t house) {
    if (house == 0) return;
    auto& map = map_for(house);
    std::fill(map.revealed.begin(), map.revealed.end(), 0);
    std::fill(map.visible.begin(), map.visible.end(), 0);
}

std::int32_t PrototypeShroud::sight_range(const rules::IniFile& rules,
    const std::string& type) const {
    return rules.read_integer(type, "Sight", 0);
}

void PrototypeShroud::tick(const session::world::Snapshot& world,
    const rules::IniFile& rules, const SightCenter& center) {
    // Every snapshot house gets a map, so queries never depend on tick order.
    for (const auto& house : world.houses) map_for(house.id);

    // Spy satellite: an eligible SpySat=yes building sees the whole map
    // (FUN_004F2FC0). Losing the last eligible one reshrouds the whole map
    // through 00559F10, then normal sight rebuilds the view; the grant
    // reveals through 0055A120.
    std::map<std::uint32_t, bool> satellite_now;
    for (const auto& house : world.houses) {
        bool active = false;
        for (const auto& entity : world.entities) {
            if (entity.house_id != house.id || entity.kind != session::world::Kind::Structure ||
                !sight_source(entity))
                continue;
            if (rules.read_bool(entity.type, "SpySat") && satellite_eligible(entity)) {
                active = true;
                break;
            }
        }
        satellite_now[house.id] = active;
        const auto previous = satellite_.find(house.id);
        if (previous != satellite_.end() && previous->second && !active)
            reshroud_all(house.id);
        satellite_[house.id] = active;
    }

    // Reset the per-frame layers, then rebuild them from the snapshot.
    for (auto& [house, map] : houses_) {
        std::fill(map.visible.begin(), map.visible.end(), 0);
        std::fill(map.gaps.begin(), map.gaps.end(), 0);
    }
    for (const auto& [house, active] : satellite_now)
        if (active) reveal_all(house);
    for (const auto& entity : world.entities) {
        if (!sight_source(entity)) continue;
        const auto range = sight_range(rules, entity.type);
        // 006D5C30 sights from the object coordinate: a building's centre.
        // TODO-ORIGINAL: RevealByHeight, height-shifted centres and veteran
        // sight bonuses are not modeled.
        if (range <= 0) continue;
        std::optional<ra2map::Cell> from;
        if (center) from = center(entity);
        sight_from(entity.house_id, from.value_or(*entity.cell), range);
    }

    // Gap generators hide their radius from every other house. The hidden
    // cells keep their explored state underneath, so the old view returns
    // when the generator dies (base IncreaseShroudCounter 0047C8A0 caps the
    // counter at the gaps covering the cell; ReduceShroudCounter 0047C840).
    // Allies of the owner keep their view: the base alliance mask House
    // +5674 holds one bit per allied house index (IsAlliedWith use in
    // 004EB360). TODO-ORIGINAL: the shroud pulse timing and
    // SuperGapRadiusInCells are not modeled, and low power is ignored. A satellite view ignores gaps, like RA1
    // DisplayClass::Shroud_Cell with GPS active.
    for (const auto& entity : world.entities) {
        if (entity.kind != session::world::Kind::Structure || !sight_source(entity)) continue;
        if (!rules.read_bool(entity.type, "GapGenerator")) continue;
        const auto range = rules.read_integer(entity.type, "GapRadiusInCells", 0);
        if (range <= 0 || !entity.cell.has_value() || !in_grid(*entity.cell)) continue;
        const auto owner = std::find_if(world.houses.begin(), world.houses.end(),
            [&](const auto& house) { return house.id == entity.house_id; });
        for (const auto& house : world.houses) {
            if (house.id == entity.house_id || map_neutral(house)) continue;
            if (owner != world.houses.end() && owner->alliance_mask_5674 &&
                house.original_index && *house.original_index >= 0 &&
                *house.original_index < 32 &&
                (*owner->alliance_mask_5674 >> *house.original_index & 1u) != 0)
                continue;
            if (satellite_now[house.id]) continue;
            auto& map = map_for(house.id);
            for_each_cell_in_diamond(*entity.cell, std::min(range, max_sight_range),
                [&](ra2map::Cell cell) {
                    if (!in_grid(cell)) return;
                    ++map.gaps[index_of(cell)];
                });
        }
    }
}

bool PrototypeShroud::cell_revealed_to(std::uint32_t house, ra2map::Cell cell) const {
    const auto* map = find_map(house);
    if (!map || !in_grid(cell)) return false;
    const auto index = index_of(cell);
    return map->revealed[index] != 0 && map->gaps[index] == 0;
}

bool PrototypeShroud::cell_visible_to(std::uint32_t house, ra2map::Cell cell) const {
    const auto* map = find_map(house);
    if (!map || !in_grid(cell)) return false;
    const auto index = index_of(cell);
    return map->visible[index] != 0 && map->gaps[index] == 0;
}

void PrototypeShroud::revealed_layer(std::uint32_t house,
    std::vector<std::uint8_t>& out) const {
    const auto cells = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    out.assign(cells, 0);
    const auto* map = find_map(house);
    if (!map) return;
    for (std::size_t index = 0; index < cells; ++index)
        out[index] = map->revealed[index] != 0 && map->gaps[index] == 0;
}

void PrototypeShroud::revealed_layer(std::uint32_t house, std::int32_t x0, std::int32_t y0,
    std::int32_t x1, std::int32_t y1, std::vector<std::uint8_t>& out) const {
    const auto width = std::max(0, x1 - x0), height = std::max(0, y1 - y0);
    out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const auto* map = find_map(house);
    if (!map) return;
    for (std::int32_t y = std::max(0, y0); y < std::min(y1, height_); ++y) {
        const auto row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
        auto* target = &out[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(width)];
        for (std::int32_t x = std::max(0, x0); x < std::min(x1, width_); ++x)
            target[x - x0] = map->revealed[row + x] != 0 && map->gaps[row + x] == 0;
    }
}

PrototypeShroud::Vision PrototypeShroud::vision(std::uint32_t house, ra2map::Cell cell) const {
    const auto* map = find_map(house);
    if (!map || !in_grid(cell)) return Vision::Hidden;
    const auto index = index_of(cell);
    if (map->gaps[index] != 0) return Vision::Hidden;
    if (map->visible[index] != 0) return Vision::Visible;
    return map->revealed[index] != 0 ? Vision::Explored : Vision::Hidden;
}

} // namespace ra2::sim
