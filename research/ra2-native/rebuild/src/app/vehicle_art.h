#pragma once
#include "../voxel/voxel.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../gfx/shp_decode.h"
#include <map>
#include <optional>

namespace ra2::app {
struct VehicleSprite { gfx::RgbaImage image; int left, top; };
// Flat base-game tank poses. The original raster tests cover HTNK, MTNK,
// and 4TNK. Other models and locomotor drawing states remain separate.
class VehicleArt {
public:
    static VehicleArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                            const rules::IniFile& rules, std::string_view type);
    const VehicleSprite& draw(std::uint8_t map_facing);
private:
    struct Part { voxel::Model model; voxel::Animation animation; };
    Part hull_;
    std::optional<Part> turret_, barrel_;
    voxel::Lighting lighting_;
    int turret_offset_ = 0;
    std::map<std::uint8_t, VehicleSprite> images_;
};
}
