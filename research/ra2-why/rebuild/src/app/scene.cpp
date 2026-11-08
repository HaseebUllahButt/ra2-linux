#include "scene.h"
#include "../sim/floor.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ra2::app {
namespace {
std::uint32_t cell_key(ra2map::Cell cell) {
    return std::uint32_t(cell.x) | (std::uint32_t(cell.y) << 16);
}
struct Theater { const char* name; char code; const char* palette; };
constexpr Theater theaters[] = {
    {"TEMPERATE", 'T', "UNITTEM.PAL"}, {"SNOW", 'A', "UNITSNO.PAL"},
    {"URBAN", 'U', "UNITURB.PAL"}, {"DESERT", 'D', "UNITDES.PAL"},
    {"NEWURBAN", 'N', "UNITUBN.PAL"}, {"LUNAR", 'L', "UNITLUN.PAL"},
};
void valid_image(const render::Bitmap& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != std::size_t(image.width) * image.height * 4)
        throw std::runtime_error("Invalid scene terrain image");
}
}

rules::IniFile overlay_rules(const rules::IniFile& base, const rules::IniFile& map) {
    std::ostringstream text;
    std::set<std::string> sections;
    auto append = [&](const rules::IniFile::Section& section) {
        if (!sections.insert(section.name).second) return;
        text << '[' << section.name << "]\n";
        std::set<std::string> keys;
        // The map's first effective value replaces the rules value.
        if (const auto* overlay = map.find_section(section.name))
            for (const auto& entry : overlay->entries)
                if (keys.insert(entry.key).second) text << entry.key << '=' << entry.value << '\n';
        for (const auto& entry : section.entries)
            if (keys.insert(entry.key).second) text << entry.key << '=' << entry.value << '\n';
    };
    for (const auto& section : base.sections()) append(section);
    for (const auto& section : map.sections()) append(section);
    rules::IniFile result;
    result.parse(text.str());
    return result;
}

Scene::Scene(const fs::MixFileSystem& files, rules::IniFile art, rules::IniFile rules,
             const render::MapGeometry& geometry, const sim::TerrainCells& terrain,
             std::string_view theater)
    : files_(files), art_(std::move(art)), rules_(std::move(rules)),
      // Original tile caller 006D7F20 projects cell*256 then subtracts 30
      // from X. Align actor projection with render_minimap's diamond origin.
      map_camera_{geometry.local.x * 60 - geometry.size.width * 30,
                  geometry.local.y * 30 + (geometry.size.width + 1) * 15}, theater_(0) {
    for (const auto& entry : theaters) if (theater == entry.name) {
        theater_ = entry.code;
        const auto bytes = files.read_file(entry.palette);
        if (!bytes) throw std::runtime_error(std::string("Missing original unit palette: ") + entry.palette);
        palette_ = gfx::Palette::from_bytes(*bytes);
    }
    if (!theater_) throw std::runtime_error("Unsupported scene theater");
    for (const auto& cell : terrain)
        if (!cells_.emplace(cell_key(cell.cell), cell).second)
            throw std::runtime_error("Duplicate scene terrain cell");
}

actors::WorldPosition Scene::position(const sim::Entity& entity) const {
    const auto found = cells_.find(cell_key(entity.cell));
    if (found == cells_.end()) throw std::runtime_error("Actor floor cell is missing");
    const auto& cell = found->second;
    if (!cell.height_level || !cell.ramp_index || !cell.floor_world_height)
        throw std::runtime_error("Actor floor height is unresolved");
    if (*cell.height_level < -128 || *cell.height_level > 127 ||
        *cell.ramp_index < 0 || *cell.ramp_index > 20)
        throw std::runtime_error("Actor floor metadata is outside original bounds");
    if (entity.kind == sim::Kind::Structure)
        return actors::building_draw_position(entity.cell, *cell.floor_world_height);
    if (entity.kind != sim::Kind::Infantry)
        throw std::runtime_error("Vehicle and aircraft scene drawing is unresolved");
    if (entity.subcell < 0 || entity.subcell >= 5)
        throw std::runtime_error("Infantry subcell is outside original bounds");
    auto result = actors::infantry_position(entity.cell, unsigned(entity.subcell), 0);
    result.z = sim::original_floor_height(static_cast<std::int8_t>(*cell.height_level),
        static_cast<std::uint8_t>(*cell.ramp_index), static_cast<std::int32_t>(result.x),
        static_cast<std::int32_t>(result.y));
    return result;
}

actors::ScreenPoint Scene::map_anchor(const sim::Entity& entity) const {
    return actors::project_world(position(entity), map_camera_);
}

SceneFrame Scene::render(const render::Bitmap& terrain, const sim::Snapshot& state,
                         render::Rectangle viewport) {
    valid_image(terrain);
    if (viewport.x < 0 || viewport.y < 0 || viewport.width <= 0 || viewport.height <= 0 ||
        viewport.width > 8192 || viewport.height > 8192 ||
        std::size_t(viewport.width) * viewport.height > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid scene viewport");
    SceneFrame result;
    result.image = {viewport.width, viewport.height,
        std::vector<std::uint8_t>(std::size_t(viewport.width) * viewport.height * 4)};
    const int copy_width = std::max(0, std::min(viewport.width, terrain.width - viewport.x));
    const int copy_height = std::max(0, std::min(viewport.height, terrain.height - viewport.y));
    for (int y = 0; copy_width > 0 && y < copy_height; ++y)
        std::copy_n(terrain.rgba.begin() + (std::size_t(y + viewport.y) * terrain.width + viewport.x) * 4,
            std::size_t(copy_width) * 4, result.image.rgba.begin() + std::size_t(y) * viewport.width * 4);

    struct Draw { const sim::Entity* entity; actors::WorldPosition position; std::int64_t key; };
    std::vector<Draw> draws;
    for (const auto& entity : state.entities) {
        if (entity.dead) continue;
        try {
            const auto world = position(entity);
            const auto key = entity.kind == sim::Kind::Structure ?
                actors::building_y_sort(world, rules_, entity.type) : actors::object_y_sort(world);
            draws.push_back({&entity, world, key});
        } catch (const std::exception& error) {
            result.gaps.push_back("Entity " + std::to_string(entity.id) + ": " + error.what());
        }
    }
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.key < b.key; });
    for (const auto& draw : draws) {
        const auto& entity = *draw.entity;
        try {
            const auto key = std::make_pair(entity.kind, entity.type);
            auto found = assets_.find(key);
            if (found == assets_.end()) {
                const auto kind = entity.kind == sim::Kind::Structure ? actors::Kind::Building : actors::Kind::Infantry;
                found = assets_.emplace(key, Asset{actors::ActorArt::load(files_, art_, rules_,
                    entity.type, kind, theater_), {}}).first;
            }
            auto& asset = found->second;
            // Only stationary, full-health main images are established here.
            // Gameplay transitions and damaged building frames remain separate.
            if (entity.map_health != 256)
                throw std::runtime_error("Damaged actor frame selection is unresolved");
            const auto frame = entity.kind == sim::Kind::Infantry ?
                asset.art.frame_for_map("Ready", static_cast<std::uint8_t>(entity.facing), 0) : 0;
            auto image = asset.frames.find(frame);
            if (image == asset.frames.end())
                image = asset.frames.emplace(frame, asset.art.draw_frame(frame, palette_)).first;
            auto anchor = actors::project_world(draw.position, map_camera_);
            anchor.x -= viewport.x;
            anchor.y -= viewport.y;
            const auto origin = asset.art.main_canvas_origin(anchor);
            const auto& sprite = image->second;
            const auto x0 = std::max<std::int64_t>(0, -origin.x);
            const auto y0 = std::max<std::int64_t>(0, -origin.y);
            const auto x1 = std::min<std::int64_t>(sprite.width, viewport.width - origin.x);
            const auto y1 = std::min<std::int64_t>(sprite.height, viewport.height - origin.y);
            bool drawn = false;
            for (auto y = y0; y < y1; ++y) for (auto x = x0; x < x1; ++x) {
                const auto source = (std::size_t(y) * sprite.width + x) * 4;
                if (!sprite.pixels[source + 3]) continue;
                const auto target = (std::size_t(y + origin.y) * viewport.width + x + origin.x) * 4;
                std::copy_n(sprite.pixels.begin() + source, 4, result.image.rgba.begin() + target);
                drawn = true;
            }
            result.drawn += drawn;
        } catch (const std::exception& error) {
            result.gaps.push_back("Entity " + std::to_string(entity.id) + ": " + error.what());
        }
    }
    for (std::size_t p = 3; p < result.image.rgba.size(); p += 4) result.image.rgba[p] = 255;
    return result;
}
} // namespace ra2::app
