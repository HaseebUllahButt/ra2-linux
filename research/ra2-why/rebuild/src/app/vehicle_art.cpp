#include "vehicle_art.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::app {
VehicleArt VehicleArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                            const rules::IniFile& rules, std::string_view type) {
    if (files.variant() != fs::GameVariant::RedAlert2)
        throw std::runtime_error("Expansion vehicle scene lighting is unresolved");
    const auto image = rules.read_string(type, "Image", type);
    auto upper = image;
    for (auto& c : upper) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (upper != "HTNK" && upper != "MTNK" && upper != "4TNK")
        throw std::runtime_error("Vehicle model is outside the verified flat tank set");
    if (!art.read_bool(image, "Voxel", false))
        throw std::runtime_error("Vehicle art does not select an original voxel model");
    VehicleArt result;
    auto load_part = [&](const std::string& name) -> std::optional<Part> {
        // Original object-type art loader 005F8110 uses Image, ImageTUR,
        // and ImageBARL with paired VXL/HVA files. Keep HVA data unscaled.
        const auto vxl = files.read_file(name + ".VXL");
        if (!vxl) return std::nullopt;
        const auto hva = files.read_file(name + ".HVA");
        if (!hva) throw std::runtime_error("Missing original vehicle animation: " + name);
        Part part{voxel::read_vxl(*vxl), voxel::read_hva(*hva)};
        if (part.animation.frames != 1)
            throw std::runtime_error("Animated vehicle frame selection is unresolved");
        return part;
    };
    auto hull = load_part(image);
    if (!hull) throw std::runtime_error("Missing original vehicle hull: " + image);
    result.hull_ = std::move(*hull);
    result.turret_ = load_part(image + "TUR");
    result.barrel_ = load_part(image + "BARL");
    const auto vpl = files.read_file("VOXELS.VPL");
    if (!vpl) throw std::runtime_error("Missing original vehicle lighting table");
    result.lighting_ = voxel::read_vpl(*vpl);
    // Base TechnoType 006D9B40 defaults to zero; 006DD9CC reads rules.
    result.turret_offset_ = rules.read_integer(type, "TurretOffset", 0);
    return result;
}

const VehicleSprite& VehicleArt::draw(std::uint8_t facing) {
    return draw(facing, facing);
}

const VehicleSprite& VehicleArt::draw(std::uint8_t facing, std::uint8_t turret_facing) {
    const auto key = std::make_pair(facing, turret_facing);
    auto found = images_.find(key);
    if (found != images_.end()) return found->second;
    auto pose = voxel::vehicle_pose_from_map(facing);
    pose.turret_facing = std::uint16_t(turret_facing) << 8;
    pose.hull.model = &hull_.model;
    pose.hull.animation = &hull_.animation;
    if (turret_) { pose.turret.model = &turret_->model; pose.turret.animation = &turret_->animation; }
    if (barrel_) { pose.barrel.model = &barrel_->model; pose.barrel.animation = &barrel_->animation; }
    pose.turret_offset = turret_offset_;
    auto rendered = voxel::draw_vehicle(pose, lighting_);
    if (rendered.image.width > 65535 || rendered.image.height > 65535)
        throw std::runtime_error("Vehicle image dimensions exceed scene bounds");
    VehicleSprite sprite{{static_cast<std::uint16_t>(rendered.image.width),
        static_cast<std::uint16_t>(rendered.image.height), {}}, rendered.left, rendered.top};
    sprite.image.pixels.reserve(rendered.image.pixels.size() * 4);
    for (const auto pixel : rendered.image.pixels) {
        sprite.image.pixels.push_back(pixel.r); sprite.image.pixels.push_back(pixel.g);
        sprite.image.pixels.push_back(pixel.b); sprite.image.pixels.push_back(pixel.a);
    }
    return images_.emplace(key, std::move(sprite)).first->second;
}
}
