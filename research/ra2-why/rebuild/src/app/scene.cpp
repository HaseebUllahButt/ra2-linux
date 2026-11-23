#include "scene.h"
#include "house_color.h"
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
    // The playable world already owns each soldier's exact walking position.
    // Rebuilding it from subcell 0 stacks soldiers at the cell centre.
    if (prototype_ && entity.position)
        return {entity.position->x, entity.position->y, entity.position->z};
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

void Scene::set_house_color(const std::string& house,int selected) {
    if(!prototype_) throw std::runtime_error("House colours require the playable scene");
    house_palettes_.insert_or_assign(selected,palette_.with_house_ramp(house_ramp(rules_,selected)));
    house_colors_[house]=selected;
}

VehicleArt& Scene::vehicle(const std::string& type) {
    const auto key=std::make_pair(type,active_color_);
    auto found=vehicles_.find(key);
    if(found==vehicles_.end()) {
        loop::profile::asset_event("vehicle",type.c_str());
        found=vehicles_.emplace(key,VehicleArt::load(files_,art_,rules_,type,prototype_)).first;
        if(active_color_>=0) found->second.set_house_ramp(house_ramp(rules_,active_color_));
    }
    return found->second;
}
bool Scene::shp_vehicle(const std::string& type) const {
    return !art_.read_bool(rules_.read_string(type, "Image", type), "Voxel", false);
}
const actors::ShpVehicleFrames& Scene::shp_layout(const std::string& type) {
    if (const auto found = shp_layouts_.find(type); found != shp_layouts_.end()) return found->second;
    // Base SHP unit draw 007005B0, no-turret body path 00700E1E.
    bool registered = false;
    if (const auto* list = rules_.find_section("VehicleTypes"))
        for (const auto& item : list->entries) registered |= item.value == type;
    if (!registered) throw std::runtime_error("SHP vehicle type is not in VehicleTypes");
    if (files_.variant() != fs::GameVariant::RedAlert2)
        throw std::runtime_error("Expansion SHP vehicle drawing is unresolved");
    if (rules_.read_bool(type, "Turret", false))
        throw std::runtime_error("SHP vehicle turret drawing is unresolved");
    if (rules_.read_bool(type, "TooBigToFitUnderBridge", false))
        throw std::runtime_error("SHP vehicle bridge drawing is unresolved");
    // TechnoType IdleRate (+0x268) animates resting units.
    if (rules_.read_integer(type, "IdleRate", 0) != 0)
        throw std::runtime_error("SHP vehicle idle animation is unresolved");
    return shp_layouts_.emplace(type, actors::shp_vehicle_frames(rules_, art_, type)).first->second;
}
Scene::ShpSprite Scene::shp_vehicle_image(const std::string& type, std::uint8_t facing, bool moving) {
    const auto& frames = shp_layout(type);
    // The walk counter (+0x430, FootClass AI 004C9279) is not
    // simulated. The prototype shows the first walk frame.
    if (moving && !prototype_) throw std::runtime_error("SHP vehicle walk counter is unresolved");
    auto& asset = actor(actors::Kind::Vehicle, type);
    return {asset, image(asset, actors::shp_vehicle_frame(frames, facing, moving, 0))};
}
Scene::Asset& Scene::actor(actors::Kind kind,const std::string& type) {
    const auto key=std::make_pair(kind,type);
    auto found=assets_.find(key);
    if(found==assets_.end()) {
        loop::profile::asset_event("actor",type.c_str(),int(kind));
        found=assets_.emplace(key,Asset{actors::ActorArt::load(files_,art_,rules_,type,kind,theater_),{}}).first;
    }
    return found->second;
}
const gfx::RgbaImage& Scene::image(Asset& asset,std::size_t frame,bool terrain) {
    const auto key=std::make_pair(frame,terrain?-1:active_color_);
    auto found=asset.frames.find(key);
    if(found==asset.frames.end()) {
        loop::profile::asset_event("shp",asset.art.filename().c_str(),int(frame));
        found=asset.frames.emplace(key,asset.art.draw_frame(frame,terrain?terrain_palette_:
            active_color_>=0?house_palettes_.at(active_color_):palette_)).first;
    }
    return found->second;
}
void Scene::prepare(const sim::Snapshot& state,const std::vector<std::string>& products) {
    active_color_=-1;
    if(!pick_ready_)prepare_pick();
    if(!terrain_ready_)prepare_terrain(terrain_gaps_);
    prepare_color(state,products);
    for(const auto& entry:house_palettes_) {
        active_color_=entry.first;
        prepare_color(state,products);
    }
    active_color_=-1;
}
void Scene::prepare_color(const sim::Snapshot& state,const std::vector<std::string>& products) {
    for(const auto& entity:state.entities)if(!entity.dead)try {
        if(entity.kind==sim::Kind::Unit) {
            if(entity.facing<0||entity.facing>255)continue;
            if(shp_vehicle(entity.type))shp_vehicle_image(entity.type,std::uint8_t(entity.facing),entity.moving);
            else {
                if(!entity.turret_facing||*entity.turret_facing<0||*entity.turret_facing>255)continue;
                vehicle(entity.type).draw(std::uint8_t(entity.facing),std::uint8_t(*entity.turret_facing));
            }
            const auto deploy=rules_.read_string(entity.type,"DeploysInto");
            if(!deploy.empty())image(actor(actors::Kind::Building,deploy),0);
        } else {
            const auto kind=entity.kind==sim::Kind::Structure?actors::Kind::Building:actors::Kind::Infantry;
            auto& asset=actor(kind,entity.type);
            const auto frame=kind==actors::Kind::Infantry?
                asset.art.frame_for_map("Ready",std::uint8_t(entity.facing),0):0;
            image(asset,frame);
        }
    } catch(const std::exception&) { /* Render reports unsupported art at its original point. */ }
    const auto registered=[&](const char* list,const std::string& type) {
        const auto* section=rules_.find_section(list);
        return section&&std::any_of(section->entries.begin(),section->entries.end(),
            [&](const auto& entry){return entry.value==type;});
    };
    // Resting SHP products: one stand frame for each of the 8 directions.
    const auto product_vehicle=[&](const std::string& type) {
        if(!shp_vehicle(type)) { vehicle(type).draw(0,0); return; }
        for(unsigned facing=0;facing<256;facing+=32)shp_vehicle_image(type,std::uint8_t(facing),false);
    };
    for(const auto& type:products)try {
        if(registered("BuildingTypes",type)) {
            image(actor(actors::Kind::Building,type),0);
            const auto free_unit=rules_.read_string(type,"FreeUnit");
            if(!free_unit.empty())product_vehicle(free_unit);
        } else if(registered("InfantryTypes",type)) {
            auto& asset=actor(actors::Kind::Infantry,type);
            for(unsigned facing=0;facing<256;facing+=32)
                image(asset,asset.art.frame_for_map("Ready",std::uint8_t(facing),0));
        } else if(registered("VehicleTypes",type))product_vehicle(type);
    } catch(const std::exception&) { /* Keep the normal render fallback. */ }
}

SceneFrame Scene::render(const render::Bitmap& terrain, const sim::Snapshot& state,
                         render::Rectangle viewport, const session::world::Snapshot* world) {
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

    if (world) {
        if (!prototype_) result.gaps.push_back("Strict resource drawing is unresolved");
        else try {
            if (!resources_) resources_ = std::make_unique<ResourceRender>(files_, rules_, art_, theater_);
            for (const auto& sprite : resources_->sprites(*world, map_camera_, result.gaps))
                result.drawn += blit(result.image, *sprite.image,
                    {sprite.origin.x - viewport.x, sprite.origin.y - viewport.y});
        } catch (const std::exception& error) { result.gaps.push_back(error.what()); }
    }

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
            const auto color=house_colors_.find(entity.house);
            active_color_=prototype_ && color!=house_colors_.end()?color->second:-1;
            if (entity.kind == sim::Kind::Unit) {
                if (entity.map_health != 256 && !prototype_)
                    throw std::runtime_error("Damaged vehicle drawing states are unresolved");
                if (entity.facing < 0 || entity.facing > 255)
                    throw std::runtime_error("Vehicle map facing is outside its original byte range");
                if (shp_vehicle(entity.type)) {
                    const auto shp = shp_vehicle_image(entity.type,
                        static_cast<std::uint8_t>(entity.facing), entity.moving);
                    result.drawn += blit(result.image, shp.image, shp.asset.art.main_canvas_origin(anchor));
                    continue;
                }
                if (!entity.turret_facing)
                    throw std::runtime_error("Vehicle turret facing is unresolved");
                if (*entity.turret_facing < 0 || *entity.turret_facing > 255)
                    throw std::runtime_error("Vehicle turret facing is outside its original byte range");
                const auto& sprite=vehicle(entity.type).draw(static_cast<std::uint8_t>(entity.facing),
                    static_cast<std::uint8_t>(*entity.turret_facing));
                result.drawn += blit(result.image, sprite.image,
                    {anchor.x + sprite.left, anchor.y + sprite.top});
                continue;
            }
            const auto kind = entity.kind == sim::Kind::Structure ? actors::Kind::Building : actors::Kind::Infantry;
            auto& asset=actor(kind,entity.type);
            // Only stationary, full-health main images are established here.
            // Gameplay transitions and damaged building frames remain separate.
            if (entity.map_health != 256 && !prototype_)
                throw std::runtime_error("Damaged actor frame selection is unresolved");
            const auto frame = kind == actors::Kind::Infantry ?
                asset.art.frame_for_map("Ready", static_cast<std::uint8_t>(entity.facing), 0) : 0;
            const auto& sprite=image(asset,frame);
            const auto origin=asset.art.main_canvas_origin(anchor);
            result.drawn+=blit(result.image,sprite,origin);
        } catch (const std::exception& error) {
            result.gaps.push_back("Entity " + std::to_string(draw.entity->id) + ": " + error.what());
        }
    }
    active_color_=-1;
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
            auto& asset=actor(actors::Kind::Terrain,type);
            const auto frame=actors::terrain_initial_frame(rules_,type);
            const auto& sprite=image(asset,frame,true);
            draw.origin=asset.art.main_canvas_origin(anchor);
            draw.sprite=&sprite;
        } catch (const std::exception& error) {
            draw.error = "Terrain " + type + ": " + error.what();
        }
    }
    terrain_ready_ = true;
}
} // namespace ra2::app
