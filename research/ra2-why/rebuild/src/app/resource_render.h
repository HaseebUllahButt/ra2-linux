#pragma once
#include "../actors/actors.h"
#include "../session/world/world.h"
#include "../fs/mix_bootstrap.h"
#include <map>
namespace ra2::app {
// Playable resource layer. Strict scenes report this unfinished draw path.
// Original lighting, slope families, cell animation and pixel depth remain unresolved.
class ResourceRender {
public:
    ResourceRender(const fs::MixFileSystem&, const rules::IniFile&, const rules::IniFile&, char theater);
    struct Sprite { const gfx::RgbaImage* image; actors::ScreenPoint origin; };
    std::vector<Sprite> sprites(const session::world::Snapshot&, actors::ScreenPoint camera,
                               std::vector<std::string>& gaps);
private:
    const fs::MixFileSystem& files_;
    rules::IniFile rules_,art_;
    gfx::Palette palette_;
    std::string suffix_;
    std::vector<std::string> types_;
    std::map<std::string,gfx::ShpImage> shapes_;
    std::map<std::pair<std::string,unsigned>,gfx::RgbaImage> images_;
};
}
