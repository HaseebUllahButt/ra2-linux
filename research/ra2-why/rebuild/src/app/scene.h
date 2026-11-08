#pragma once

#include "../actors/actors.h"
#include "../render/minimap.h"
#include "../sim/sim.h"
#include "vehicle_art.h"
#include <map>
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
          std::string_view theater, std::vector<ra2map::TerrainObject> objects = {});
    SceneFrame render(const render::Bitmap& terrain, const sim::Snapshot& state,
                      render::Rectangle viewport);
    actors::ScreenPoint map_anchor(const sim::Entity& entity) const;
private:
    struct Asset {
        actors::ActorArt art;
        std::map<std::size_t, gfx::RgbaImage> frames;
    };
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
};
} // namespace ra2::app
