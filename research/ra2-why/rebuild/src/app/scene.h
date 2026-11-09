#pragma once

#include "../actors/actors.h"
#include "../render/minimap.h"
#include "../sim/sim.h"
#include "vehicle_art.h"
#include <map>
#include <optional>
#include <string>

namespace ra2::app {
// Apply effective map keys over effective rules keys. Preserve the existing
// INI reader's first-section and first-key behavior inside each source.
rules::IniFile overlay_rules(const rules::IniFile& base, const rules::IniFile& map);

struct SceneFrame {
    render::Bitmap image;
    std::size_t drawn = 0;
    std::vector<std::string> gaps;
};

// Native scene integration for stationary SHP actors and flat base tanks.
// Main images use original object order. Pixel depth, shadows, house colors,
// world lighting, damaged states, and attached animations remain separate.
class Scene {
public:
    Scene(const fs::MixFileSystem& files, rules::IniFile art, rules::IniFile rules,
          const render::MapGeometry& geometry, const sim::TerrainCells& terrain,
          std::string_view theater, std::vector<ra2map::TerrainObject> objects = {},
          bool prototype = false);
    SceneFrame render(const render::Bitmap& terrain, const sim::Snapshot& state,
                      render::Rectangle viewport);
    actors::ScreenPoint map_anchor(const sim::Entity& entity) const;
    // The floor Cell nearest to a map pixel, by |dx|+2|dy| between the pixel
    // and the Cell's centre anchor, below 46. Ties pick the lowest packed key
    // (y*512+x). Same result as scanning every Cell with a floor height, but
    // it only tests Cells in nearby grid buckets. Floor heights are fixed
    // during a match, so the buckets are built once on the first call.
    std::optional<ra2map::Cell> pick_cell(std::int64_t map_x, std::int64_t map_y) const;
private:
    struct PickCell { std::int64_t x, y; std::uint32_t key; ra2map::Cell cell; };
    void prepare_pick() const;
    mutable bool pick_ready_ = false;
    mutable std::string pick_error_;
    mutable std::int64_t pick_left_ = 0, pick_top_ = 0;
    mutable int pick_columns_ = 0, pick_rows_ = 0;
    mutable std::vector<std::vector<PickCell>> pick_buckets_;
    struct Asset {
        actors::ActorArt art;
        std::map<std::size_t, gfx::RgbaImage> frames;
    };
    // One map terrain object. Its position, sort key and art never change
    // during a match, so they are resolved once on the first render.
    struct TerrainDraw {
        const ra2map::TerrainObject* object = nullptr;
        actors::WorldPosition position{};
        std::int64_t key = 0;
        const gfx::RgbaImage* sprite = nullptr; // Null with an error.
        actors::ScreenPoint origin{};           // Sprite origin in map pixels.
        std::string error;                      // Draw error, repeated each frame.
    };
    void prepare_terrain(std::vector<std::string>& position_gaps);
    actors::WorldPosition position(const sim::Entity& entity) const;
    const fs::MixFileSystem& files_;
    rules::IniFile art_, rules_;
    gfx::Palette palette_, terrain_palette_;
    actors::ScreenPoint map_camera_;
    char theater_;
    std::map<std::uint32_t, sim::TerrainCell> cells_;
    std::map<std::pair<actors::Kind, std::string>, Asset> assets_;
    std::map<std::string, VehicleArt> vehicles_;
    std::vector<ra2map::TerrainObject> terrain_objects_;
    bool terrain_ready_ = false;
    std::vector<TerrainDraw> terrain_draws_;   // Stable-sorted by key.
    std::vector<std::string> terrain_gaps_;    // Position errors, in object order.
    bool prototype_ = false;
};
} // namespace ra2::app
