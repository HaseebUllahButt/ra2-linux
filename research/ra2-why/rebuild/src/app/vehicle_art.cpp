#include "vehicle_art.h"
#include "../loop/profile.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::app {
VehicleArt VehicleArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                            const rules::IniFile& rules, std::string_view type, bool prototype) {
    if (files.variant() != fs::GameVariant::RedAlert2)
        throw std::runtime_error("Expansion vehicle scene lighting is unresolved");
    const auto image = rules.read_string(type, "Image", type);
    auto upper = image;
    for (auto& c : upper) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    // TODO-ORIGINAL: recover each model's locomotor pose, damage and lighting.
    // The playable path uses the same flat voxel pose for other base vehicles.
    if (!prototype && upper != "HTNK" && upper != "MTNK" && upper != "4TNK")
        throw std::runtime_error("Vehicle model is outside the verified flat tank set");
    if (!art.read_bool(image, "Voxel", false))
        throw std::runtime_error("Vehicle art does not select an original voxel model");
    VehicleArt result;result.type_=std::string(type);
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

void VehicleArt::set_house_ramp(const std::array<gfx::Rgba,16>& ramp) {
    auto colors=lighting_.palette;
    for(unsigned i=0;i<16;++i) colors[16+i]=ramp[i];
    lighting_=voxel::team_colors(lighting_,colors);
    images_.clear(); hull_layers_.clear(); upper_layers_.clear();
}

const VehicleSprite& VehicleArt::draw(std::uint8_t facing) {
    return draw(facing, facing);
}

voxel::VehiclePose VehicleArt::pose(std::uint8_t facing, std::uint8_t turret_facing) const {
    auto pose = voxel::vehicle_pose_from_map(facing);
    pose.turret_facing = std::uint16_t(turret_facing) << 8;
    pose.hull.model = &hull_.model;
    pose.hull.animation = &hull_.animation;
    if (turret_) { pose.turret.model = &turret_->model; pose.turret.animation = &turret_->animation; }
    if (barrel_) { pose.barrel.model = &barrel_->model; pose.barrel.animation = &barrel_->animation; }
    pose.turret_offset = turret_offset_;
    return pose;
}

static std::vector<voxel::VehicleLayer> crop(const std::vector<voxel::SceneImage>& layers) {
    std::vector<voxel::VehicleLayer> result;
    result.reserve(layers.size());
    for (const auto& layer : layers) result.push_back(voxel::crop_vehicle_layer(layer));
    return result;
}

const std::vector<voxel::VehicleLayer>& VehicleArt::hull_layers(std::uint8_t facing) {
    auto found = hull_layers_.find(facing);
    if (found != hull_layers_.end()) return found->second;
    // The hull layer ignores the turret facing.
    return hull_layers_.emplace(facing,
        crop(voxel::draw_vehicle_hull_layers(pose(facing, facing), lighting_))).first->second;
}

const std::vector<voxel::VehicleLayer>& VehicleArt::upper_layers(std::uint8_t facing) {
    auto found = upper_layers_.find(facing);
    if (found != upper_layers_.end()) return found->second;
    // Turret and barrel layers ignore the hull facing.
    return upper_layers_.emplace(facing,
        crop(voxel::draw_vehicle_upper_layers(pose(facing, facing), lighting_))).first->second;
}

const VehicleSprite& VehicleArt::draw(std::uint8_t facing, std::uint8_t turret_facing) {
    const auto key = std::make_pair(facing, turret_facing);
    auto found = images_.find(key);
    if (found != images_.end()) return found->second;
    loop::profile::asset_event("pose",type_.c_str(),facing,turret_facing);
    std::vector<const voxel::VehicleLayer*> layers;
    for (const auto& layer : hull_layers(facing)) layers.push_back(&layer);
    for (const auto& layer : upper_layers(turret_facing)) layers.push_back(&layer);
    const auto rendered = voxel::compose_vehicle_layers(layers);
    if (rendered.image.width > 65535 || rendered.image.height > 65535)
        throw std::runtime_error("Vehicle image dimensions exceed scene bounds");
    VehicleSprite sprite{{static_cast<std::uint16_t>(rendered.image.width),
        static_cast<std::uint16_t>(rendered.image.height), {}}, rendered.left, rendered.top};
    sprite.image.pixels.resize(rendered.image.pixels.size() * 4);
    auto* out = sprite.image.pixels.data();
    for (const auto pixel : rendered.image.pixels) {
        out[0] = pixel.r; out[1] = pixel.g; out[2] = pixel.b; out[3] = pixel.a;
        out += 4;
    }
    return images_.emplace(key, std::move(sprite)).first->second;
}
}
