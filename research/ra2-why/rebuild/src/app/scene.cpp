#include "scene.h"
#include "../loop/profile.h"
#include "../sim/floor.h"

#include <algorithm>
#include <mutex>
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
bool blit(render::Bitmap& target, const gfx::RgbaImage& sprite, actors::ScreenPoint origin) {
    const auto x0 = std::max<std::int64_t>(0, -origin.x);
    const auto y0 = std::max<std::int64_t>(0, -origin.y);
    const auto x1 = std::min<std::int64_t>(sprite.width, target.width - origin.x);
    const auto y1 = std::min<std::int64_t>(sprite.height, target.height - origin.y);
    bool drawn = false;
    for (auto y = y0; y < y1; ++y) for (auto x = x0; x < x1; ++x) {
        const auto source = (std::size_t(y) * sprite.width + x) * 4;
        if (!sprite.pixels[source + 3]) continue;
        const auto dest = (std::size_t(y + origin.y) * target.width + x + origin.x) * 4;
        std::copy_n(sprite.pixels.begin() + source, 4, target.rgba.begin() + dest);
        drawn = true;
    }
    return drawn;
}
}

namespace {
rules::IniFile compute_overlay_rules(const rules::IniFile& base, const rules::IniFile& map);
// Startup asks for the same overlays several times. Keep recent results by
// input content. A returned copy has a new revision, so remember which
// stored result it holds; a later call with that copy as input still hits.
// Content ids are input revisions or entry tokens; both never repeat.
struct OverlayCache {
    struct Entry { std::uint64_t base, map, token; rules::IniFile result; };
    std::mutex lock;
    std::map<std::uint64_t, std::uint64_t> content; // Copy revision -> entry token.
    std::vector<Entry> entries;
    std::uint64_t tokens = 0;
    std::uint64_t id(std::uint64_t revision) const {
        const auto found = content.find(revision);
        return found == content.end() ? revision : found->second;
    }
};
OverlayCache& overlay_cache() { static OverlayCache cache; return cache; }
}

rules::IniFile overlay_rules(const rules::IniFile& base, const rules::IniFile& map) {
    auto& cache = overlay_cache();
    std::lock_guard<std::mutex> guard(cache.lock);
    const auto base_id = cache.id(base.revision()), map_id = cache.id(map.revision());
    auto found = std::find_if(cache.entries.begin(), cache.entries.end(),
        [&](const OverlayCache::Entry& entry) { return entry.base == base_id && entry.map == map_id; });
    if (found == cache.entries.end()) {
        if (cache.entries.size() >= 8) cache.entries.erase(cache.entries.begin());
        auto result = compute_overlay_rules(base, map);
        // Tokens use the top bit, so they never equal an IniFile revision.
        const auto token = (std::uint64_t(1) << 63) | ++cache.tokens;
        cache.entries.push_back({base_id, map_id, token, std::move(result)});
        found = cache.entries.end() - 1;
    }
    rules::IniFile result = found->result;
    cache.content[result.revision()] = found->token;
    return result;
}

namespace {
rules::IniFile compute_overlay_rules(const rules::IniFile& base, const rules::IniFile& map) {
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
}

Scene::Scene(const fs::MixFileSystem& files, rules::IniFile art, rules::IniFile rules,
             const render::MapGeometry& geometry, const sim::TerrainCells& terrain,
             std::string_view theater, std::vector<ra2map::TerrainObject> objects, bool prototype)
    : files_(files), art_(std::move(art)), rules_(std::move(rules)),
      // Original tile caller 006D7F20 projects cell*256 then subtracts 30
      // from X. Align actor projection with render_minimap's diamond origin.
      map_camera_{geometry.local.x * 60 - geometry.size.width * 30,
                  geometry.local.y * 30 + (geometry.size.width + 1) * 15}, theater_(0),
      terrain_objects_(std::move(objects)), prototype_(prototype) {
    for (const auto& entry : theaters) if (theater == entry.name) {
        theater_ = entry.code;
        const auto bytes = files.read_file(entry.palette);
        if (!bytes) throw std::runtime_error(std::string("Missing original unit palette: ") + entry.palette);
        palette_ = gfx::Palette::from_bytes(*bytes);
        // Terrain draw 0071C1B0 uses the cell conversion from 00544E70.
        // Its source palette is ISO<extension>.PAL, loaded at 00545150.
        const auto iso_name = "ISO" + std::string(entry.palette + 4);
        const auto iso = files.read_file(iso_name);
        if (!iso) throw std::runtime_error("Missing original terrain object palette: " + iso_name);
        terrain_palette_ = gfx::Palette::from_bytes(*iso);
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
    if (entity.kind == sim::Kind::Unit) {
        if (!entity.position) throw std::runtime_error("Vehicle world position is unresolved");
        if (*cell.ramp_index != 0 && !prototype_) throw std::runtime_error("Vehicle slope drawing is unresolved");
        return {entity.position->x, entity.position->y, entity.position->z};
    }
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

namespace {
constexpr int pick_bucket = 64; // Pixels per grid bucket side.
}
void Scene::prepare_pick() const {
    pick_ready_ = true;
    std::vector<PickCell> all;
    all.reserve(cells_.size());
    try {
        for (const auto& [key, cell] : cells_) {
            (void)key;
            if (!cell.floor_world_height) continue;
            sim::Entity probe;
            probe.cell = cell.cell; probe.kind = sim::Kind::Unit;
            probe.position = sim::Coord{int(cell.cell.x) * 256 + 128, int(cell.cell.y) * 256 + 128, *cell.floor_world_height};
            const auto anchor = map_anchor(probe);
            all.push_back({std::int64_t(int(anchor.x)), std::int64_t(int(anchor.y)),
                std::uint32_t(cell.cell.y) * 512 + cell.cell.x, cell.cell});
        }
    } catch (const std::exception& error) {
        // The full scan throws on such a Cell for every point. Keep that.
        pick_error_ = error.what();
        return;
    }
    if (all.empty()) return;
    pick_left_ = all.front().x; pick_top_ = all.front().y;
    std::int64_t right = pick_left_, bottom = pick_top_;
    for (const auto& item : all) {
        pick_left_ = std::min(pick_left_, item.x); pick_top_ = std::min(pick_top_, item.y);
        right = std::max(right, item.x); bottom = std::max(bottom, item.y);
    }
    pick_columns_ = int((right - pick_left_) / pick_bucket) + 1;
    pick_rows_ = int((bottom - pick_top_) / pick_bucket) + 1;
    pick_buckets_.assign(std::size_t(pick_columns_) * pick_rows_, {});
    for (const auto& item : all)
        pick_buckets_[std::size_t((item.y - pick_top_) / pick_bucket) * pick_columns_ +
            std::size_t((item.x - pick_left_) / pick_bucket)].push_back(item);
}

std::optional<ra2map::Cell> Scene::pick_cell(std::int64_t map_x, std::int64_t map_y) const {
    if (!pick_ready_) prepare_pick();
    if (!pick_error_.empty()) throw std::runtime_error(pick_error_);
    if (pick_buckets_.empty()) return {};
    // A match needs |dx| < 46 and |dy| < 23.
    const auto column = [&](std::int64_t x) { return std::clamp<std::int64_t>((x - pick_left_) / pick_bucket, 0, pick_columns_ - 1); };
    const auto row = [&](std::int64_t y) { return std::clamp<std::int64_t>((y - pick_top_) / pick_bucket, 0, pick_rows_ - 1); };
    if (map_x + 46 < pick_left_ || map_y + 23 < pick_top_) return {};
    int best = 46; std::uint32_t best_key = 0; std::optional<ra2map::Cell> result;
    for (auto r = row(map_y - 23); r <= row(map_y + 23); ++r)
        for (auto c = column(map_x - 46); c <= column(map_x + 46); ++c)
            for (const auto& item : pick_buckets_[std::size_t(r) * pick_columns_ + std::size_t(c)]) {
                const auto distance = std::abs(item.x - map_x) + 2 * std::abs(item.y - map_y);
                if (distance < best || (result && distance == best && item.key < best_key)) {
                    best = int(distance); best_key = item.key; result = item.cell;
                }
            }
    return result;
}

SceneFrame Scene::render(const render::Bitmap& terrain, const sim::Snapshot& state,
                         render::Rectangle viewport) {
    loop::profile::Timer probe(loop::profile::Phase::Scene);
    valid_image(terrain);
    if (viewport.x < 0 || viewport.y < 0 || viewport.width <= 0 || viewport.height <= 0 ||
        viewport.width > 8192 || viewport.height > 8192 ||
        std::size_t(viewport.width) * viewport.height > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid scene viewport");
    SceneFrame result;
    const int copy_width = std::max(0, std::min(viewport.width, terrain.width - viewport.x));
    const int copy_height = std::max(0, std::min(viewport.height, terrain.height - viewport.y));
    const auto size = std::size_t(viewport.width) * viewport.height * 4;
    std::vector<std::uint8_t> pixels;
    if (copy_width == viewport.width && copy_height == viewport.height) {
        // The terrain covers the whole view: append its rows without first
        // clearing the buffer.
        pixels.reserve(size);
        for (int y = 0; y < copy_height; ++y) {
            const auto row = terrain.rgba.begin() + (std::size_t(y + viewport.y) * terrain.width + viewport.x) * 4;
            pixels.insert(pixels.end(), row, row + std::size_t(copy_width) * 4);
        }
    } else {
        pixels.assign(size, 0);
        for (int y = 0; copy_width > 0 && y < copy_height; ++y)
            std::copy_n(terrain.rgba.begin() + (std::size_t(y + viewport.y) * terrain.width + viewport.x) * 4,
                std::size_t(copy_width) * 4, pixels.begin() + std::size_t(y) * viewport.width * 4);
    }
    result.image = {viewport.width, viewport.height, std::move(pixels)};

    struct Draw {
        const sim::Entity* entity;
        actors::WorldPosition position;
        std::int64_t key;
    };
    std::vector<Draw> draws;
    draws.reserve(state.entities.size());
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
    if (!terrain_ready_) prepare_terrain(terrain_gaps_);
    result.gaps.insert(result.gaps.end(), terrain_gaps_.begin(), terrain_gaps_.end());
    // Entities come before terrain objects in the original single list, and
    // that list is stable-sorted by key. Merging the two stable-sorted lists,
    // with entities first on equal keys, gives the same order.
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) { return a.key < b.key; });
    auto draw_terrain = [&](const TerrainDraw& item) {
        if (!item.sprite) { result.gaps.push_back(item.error); return; }
        result.drawn += blit(result.image, *item.sprite,
            {item.origin.x - viewport.x, item.origin.y - viewport.y});
    };
    auto next_terrain = terrain_draws_.begin();
    for (const auto& draw : draws) {
        for (; next_terrain != terrain_draws_.end() && next_terrain->key < draw.key; ++next_terrain) draw_terrain(*next_terrain);
        try {
            auto anchor = actors::project_world(draw.position, map_camera_);
            anchor.x -= viewport.x;
            anchor.y -= viewport.y;
            const auto& entity = *draw.entity;
            if (entity.kind == sim::Kind::Unit) {
                if (entity.map_health != 256 && !prototype_)
                    throw std::runtime_error("Damaged vehicle drawing states are unresolved");
                if (entity.facing < 0 || entity.facing > 255)
                    throw std::runtime_error("Vehicle map facing is outside its original byte range");
                if (!entity.turret_facing)
                    throw std::runtime_error("Vehicle turret facing is unresolved");
                if (*entity.turret_facing < 0 || *entity.turret_facing > 255)
                    throw std::runtime_error("Vehicle turret facing is outside its original byte range");
                auto asset = vehicles_.find(entity.type);
                if (asset == vehicles_.end())
                    asset = vehicles_.emplace(entity.type, VehicleArt::load(files_, art_, rules_, entity.type, prototype_)).first;
                const auto& sprite = asset->second.draw(static_cast<std::uint8_t>(entity.facing),
                    static_cast<std::uint8_t>(*entity.turret_facing));
                result.drawn += blit(result.image, sprite.image,
                    {anchor.x + sprite.left, anchor.y + sprite.top});
                continue;
            }
            const auto kind = entity.kind == sim::Kind::Structure ? actors::Kind::Building : actors::Kind::Infantry;
            const auto key = std::make_pair(kind, entity.type);
            auto found = assets_.find(key);
            if (found == assets_.end()) {
                found = assets_.emplace(key, Asset{actors::ActorArt::load(files_, art_, rules_,
                    entity.type, kind, theater_), {}}).first;
            }
            auto& asset = found->second;
            // Only stationary, full-health main images are established here.
            // Gameplay transitions and damaged building frames remain separate.
            if (entity.map_health != 256 && !prototype_)
                throw std::runtime_error("Damaged actor frame selection is unresolved");
            const auto frame = kind == actors::Kind::Infantry ?
                asset.art.frame_for_map("Ready", static_cast<std::uint8_t>(entity.facing), 0) : 0;
            auto image = asset.frames.find(frame);
            if (image == asset.frames.end())
                image = asset.frames.emplace(frame, asset.art.draw_frame(frame, palette_)).first;
            const auto origin = asset.art.main_canvas_origin(anchor);
            result.drawn += blit(result.image, image->second, origin);
        } catch (const std::exception& error) {
            result.gaps.push_back("Entity " + std::to_string(draw.entity->id) + ": " + error.what());
        }
    }
    for (; next_terrain != terrain_draws_.end(); ++next_terrain) draw_terrain(*next_terrain);
    // Local pointer and size: byte stores cannot then alias the vector itself.
    auto* const rgba = result.image.rgba.data();
    for (std::size_t p = 3; p < size; p += 4) rgba[p] = 255;
    return result;
}

void Scene::prepare_terrain(std::vector<std::string>& position_gaps) {
    // Same checks and messages as the per-frame pass they replace.
    for (const auto& object : terrain_objects_) {
        try {
            const auto cell = cells_.find(cell_key(object.cell));
            if (cell == cells_.end() || !cell->second.floor_world_height)
                throw std::runtime_error("Terrain object floor height is unresolved");
            // Tiberium spawners use a different palette and a -16 Y adjustment.
            if (rules_.read_bool(object.type, "SpawnsTiberium", false))
                throw std::runtime_error("Tiberium spawner drawing is unresolved");
            const auto world = actors::terrain_position(object.cell, *cell->second.floor_world_height);
            TerrainDraw draw;
            draw.object = &object; draw.position = world; draw.key = actors::object_y_sort(world);
            terrain_draws_.push_back(std::move(draw));
        } catch (const std::exception& error) {
            position_gaps.push_back("Terrain " + object.type + ": " + error.what());
        }
    }
    std::stable_sort(terrain_draws_.begin(), terrain_draws_.end(),
        [](const TerrainDraw& a, const TerrainDraw& b) { return a.key < b.key; });
    for (auto& draw : terrain_draws_) {
        const auto& type = draw.object->type;
        try {
            const auto anchor = actors::project_world(draw.position, map_camera_);
            const auto key = std::make_pair(actors::Kind::Terrain, type);
            auto found = assets_.find(key);
            if (found == assets_.end())
                found = assets_.emplace(key, Asset{actors::ActorArt::load(files_, art_, rules_,
                    type, actors::Kind::Terrain, theater_), {}}).first;
            auto& asset = found->second;
            const auto frame = actors::terrain_initial_frame(rules_, type);
            auto image = asset.frames.find(frame);
            if (image == asset.frames.end())
                image = asset.frames.emplace(frame, asset.art.draw_frame(frame, terrain_palette_)).first;
            draw.origin = asset.art.main_canvas_origin(anchor);
            draw.sprite = &image->second;
        } catch (const std::exception& error) {
            draw.error = "Terrain " + type + ": " + error.what();
        }
    }
    terrain_ready_ = true;
}
} // namespace ra2::app
