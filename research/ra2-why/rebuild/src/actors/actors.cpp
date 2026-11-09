#include "actors.h"
#include <cstdio>
#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ra2::actors {
namespace {
// 0x0051d6f0 selects the delay byte from the original sequence table at 0x007eaf7c.
int sequence_delay(std::string_view name) {
    static constexpr std::array<const char*, 42> names = {"Ready","Guard","Prone","Walk","FireUp","Down","Crawl","Up","FireProne","Idle1","Idle2","Die1","Die2","Die3","Die4","Die5","Tread","Swim","WetIdle1","WetIdle2","WetDie1","WetDie2","WetAttack","Hover","Fly","Tumble","FireFly","Deploy","Deployed","DeployedFire","DeployedIdle","Undeploy","Cheer","Paradrop","AirDeathStart","AirDeathFalling","AirDeathFinish","Panic","Shovel","Carry","SecondaryFire","SecondaryProne"};
    static constexpr std::array<int, 42> delays = {0,0,6,3,1,1,1,1,1,3,3,1,1,1,1,1,3,1,3,3,1,1,1,2,1,1,1,1,1,1,1,1,3,1,3,1,3,4,6,3,1,1};
    for (std::size_t i = 0; i < names.size(); ++i) if (name == names[i]) return delays[i];
    return -1;
}
bool speed_scaled_delay(std::string_view name) {
    return name == "Idle1" || name == "Idle2" || name == "WetIdle1" || name == "WetIdle2" || name == "Hover" || name == "Cheer";
}
}
std::string_view infantry_motion_sequence(InfantryMotion motion, bool prone, bool water) noexcept {
    // 0x00520AE0 selects Ready/Walk or Prone/Crawl based on locomotion.
    // 0x0051D6F0 maps those states to Tread/Swim in the water branch.
    if (water) return motion == InfantryMotion::Walking ? "Swim" : "Tread";
    if (motion == InfantryMotion::Walking) return prone ? "Crawl" : "Walk";
    return prone ? "Prone" : "Ready";
}
unsigned infantry_direction(std::uint8_t map_facing) noexcept {
    // 0x006F6D94 stores map facing in the high byte of the direction.
    // 0x00518F52 rounds it to 32 directions and indexes table 0x007EAEFC.
    static constexpr unsigned facing[32] = {7,7,6,6,6,6,5,5,5,5,4,4,4,4,3,3,
                                             3,3,2,2,2,2,1,1,1,1,0,0,0,0,7,7};
    return facing[((unsigned(map_facing) / 4 + 1) / 2) & 31];
}
int projected_height(std::int32_t z) noexcept {
    // 0x006D1830 and 0x006D18C0 initialize the diagonal and 60-degree
    // angle. 0x006D1BB0 uses the engine's square-root and sine tables.
    // These are the exact float table outputs at the fixed input values.
    constexpr double diagonal = 362.038665771484375;
    constexpr double sine = 0.86589753627777099609375;
    constexpr double scale = (60.0 / diagonal) * sine;
    // 0x006D1F10 adds one at height 728, then 0.5 before truncation.
    return static_cast<int>(z * scale + (z >= 728 ? 1.0 : 0.0) + 0.5);
}
ScreenPoint project_world(WorldPosition position, ScreenPoint viewport) {
    if (position.z < std::numeric_limits<std::int32_t>::min() ||
        position.z > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("world height out of range");
    return project_world(position, projected_height(static_cast<std::int32_t>(position.z)), viewport);
}
ScreenPoint project_world(WorldPosition position, int height_pixels, ScreenPoint viewport) {
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    if (position.x < low || position.x > high || position.y < low || position.y > high ||
        viewport.x < low || viewport.x > high || viewport.y < low || viewport.y > high)
        throw std::runtime_error("world projection coordinates out of range");
    // 0x006D2140 projects world XY, subtracts projected Z and viewport.
    const auto x = (position.x * 30 - position.y * 30) / 256;
    const auto y = (position.x * 15 + position.y * 15) / 256;
    return {x - viewport.x, y - height_pixels - viewport.y};
}
std::int64_t object_y_sort(WorldPosition position, int adjustment) {
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    if (position.x < low || position.x > high || position.y < low || position.y > high)
        throw std::runtime_error("sort coordinates out of range");
    // 0x005F6BD0 adds the X and Y components of the draw-position virtual.
    return position.x + position.y + adjustment;
}
std::int64_t building_y_sort(WorldPosition position, bool turret_anim_is_voxel, bool gate) {
    // 0x00449410 adds these building-type dependent offsets.
    return object_y_sort(position, (turret_anim_is_voxel ? 32 : 0) - (gate ? 16 : 0));
}
std::int64_t building_y_sort(WorldPosition position, const rules::IniFile& rules,
                             std::string_view type) {
    // 0x00460A00 and 0x00464638 read these building rules.
    return building_y_sort(position, rules.read_bool(type, "TurretAnimIsVoxel", false),
                           rules.read_bool(type, "Gate", false));
}
WorldPosition infantry_position(ra2map::Cell cell, unsigned subcell, std::int32_t z) {
    // 0x0048E480 initializes the five offsets. 0x0051FB00 adds them
    // to the cell coordinates and obtains the terrain height separately.
    static constexpr int offsets[5][2] = {{128,128},{64,64},{192,64},{64,192},{192,192}};
    if (subcell >= 5) throw std::runtime_error("invalid infantry subcell");
    return {std::int64_t(cell.x) * 256 + offsets[subcell][0],
            std::int64_t(cell.y) * 256 + offsets[subcell][1], z};
}
WorldPosition terrain_position(ra2map::Cell cell, std::int32_t floor) {
    // 0071BB90 supplies cell*256+128 and Z=0. 0071E0D0 keeps the
    // greater of that Z and GetFloorHeight. 0041BE00 preserves draw XYZ.
    return {std::int64_t(cell.x) * 256 + 128, std::int64_t(cell.y) * 256 + 128,
            std::max(0, floor)};
}
std::size_t terrain_initial_frame(const rules::IniFile& rules, std::string_view type) {
    // 0071DA80 defaults IsAnimated to false. The initial animated state is
    // outside this helper. 0071C1B0 uses frame 1 for initial health < 2.
    if (rules.read_bool(type, "IsAnimated", false))
        throw std::runtime_error("animated terrain frame state is unresolved");
    // TerrainType constructors (base 006E5260, expansion 0071DA80) set
    // Strength to -1. After the ObjectType INI read, TerrainType ReadINI
    // (base 006E5642..6E5663, expansion 0071DEC8..71DEDC) replaces -1 with
    // Rules TreeStrength. Rules constructors (00640B80, 00665650) default
    // TreeStrength to 25. Terrain constructor 0071BB90 copies Strength to Health.
    auto strength = rules.read_integer(type, "Strength", -1);
    if (strength == -1) strength = rules.read_integer("General", "TreeStrength", 25);
    return strength < 2 ? 1 : 0;
}
WorldPosition building_draw_position(ra2map::Cell cell, std::int32_t z) {
    // 0x0044F820 stores cell*256+128. The building draw-position virtual
    // at 0x00459EF0 subtracts 128 from both coordinates.
    return {std::int64_t(cell.x) * 256, std::int64_t(cell.y) * 256, z};
}
WorldPosition building_center(ra2map::Cell cell, unsigned width,
                              unsigned height, std::int32_t z) {
    if (!width || !height || width > 256 || height > 256)
        throw std::runtime_error("invalid foundation size");
    // 0x0044F820 converts cells to world coordinates. 0x00447AC0
    // offsets the building center by half of the extra foundation cells.
    return {std::int64_t(cell.x) * 256 + 128 + std::int64_t(width - 1) * 128,
            std::int64_t(cell.y) * 256 + 128 + std::int64_t(height - 1) * 128, z};
}
ActorArt::ActorArt(gfx::ShpImage image, std::string filename, Kind kind, bool remappable)
    : image_(std::move(image)), filename_(std::move(filename)), kind_(kind), remappable_(remappable) {}

ActorArt ActorArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                        const rules::IniFile& rules, std::string_view type,
                        Kind kind, char theater_code) {
    if (!rules.find_section(type) && kind != Kind::Effect) {
        // 0x00672660 registers BuildingTypes even without a type section.
        const char* registry = kind == Kind::Building ? "BuildingTypes" :
                               kind == Kind::Infantry ? "InfantryTypes" : "TerrainTypes";
        bool found = false;
        if (const auto* section = rules.find_section(registry))
            for (const auto& entry : section->entries) if (entry.value == type) { found = true; break; }
        if (!found) throw std::runtime_error("unregistered actor type");
    }
    auto image = rules.read_string(type, "Image", type);
    const auto shape = art.read_string(image, "Image", image);
    // 0x0045f230 chooses the theater suffix for Theater=yes art.
    std::string extension = "SHP";
    if (art.read_bool(image, "Theater", kind == Kind::Terrain)) {
        switch (theater_code) {
        case 'T': extension="TEM"; break; case 'A': extension="SNO"; break;
        case 'U': extension="URB"; break; case 'D': extension="DES"; break;
        case 'N': extension="UBN"; break; case 'L': extension="LUN"; break;
        default: throw std::runtime_error("invalid theater code");
        }
    }
    auto filename = shape + "." + extension;
    // 0x005f96b0 changes the second letter only for these theater names.
    const bool theater_name = shape.size() >= 2 &&
        (shape[0] == 'G' || shape[0] == 'N' || shape[0] == 'C' || shape[0] == 'Y') &&
        (shape[1] == 'A' || shape[1] == 'T');
    if (theater_name) {
        if (theater_code < 'A' || theater_code > 'Z') throw std::runtime_error("invalid theater code");
        filename[1] = theater_code;
    }
    auto bytes = files.read_file(filename);
    if (!bytes && theater_name) {
        // 0x005f9710 and 0x0045f230 retry the generic G variant.
        filename[1] = 'G';
        bytes = files.read_file(filename);
    }
    if (!bytes) throw std::runtime_error("actor SHP not found: " + filename);
    ActorArt result(gfx::ShpImage::parse(std::move(*bytes)), filename, kind,
                    art.read_bool(image, "Remapable", false));
    if (kind == Kind::Effect && art.read_integer(image, "LoopCount", 0) == -1 &&
        !art.read_bool(image, "Reverse", false) && !art.read_bool(image, "PingPong", false) &&
        !art.contains(image, "RandomRate")) {
        // 0x00427F66 converts Rate to a tick delay; 0x00423AC0 wraps
        // continuing animations from LoopEnd back to LoopStart.
        const int start = art.read_integer(image, "Start", 0);
        const int loop_start = art.read_integer(image, "LoopStart", 0);
        const int loop_end = art.read_integer(image, "LoopEnd", 0);
        const int rate = art.read_integer(image, "Rate", -1);
        if (start >= 0 && loop_start >= start && loop_end > loop_start &&
            loop_end <= result.image_.header().frame_count && rate > 0 && rate <= 900)
            result.effect_timing_ = EffectTiming{start, loop_start, loop_end, 900 / rate};
    }
    if (kind == Kind::Infantry) {
        const auto name = art.read_string(image, "Sequence");
        const auto* section = art.find_section(name);
        if (!section) throw std::runtime_error("infantry sequence missing");
        // 0x00523d00 reads first, count, stride and optional forced direction.
        for (const auto& entry : section->entries) {
            int first = 0, count = 0, stride = 0;
            char facing[32] = {};
            const int fields = std::sscanf(entry.value.c_str(), "%d , %d , %d , %31s", &first, &count, &stride, facing);
            if (fields < 3) continue; // Sequence sound entries are not frame records.
            if (first < 0 || count < 0 || stride < 0) throw std::runtime_error("invalid infantry sequence");
            result.sequences_.emplace(entry.key, Sequence{first, count, stride, facing, sequence_delay(entry.key)});
        }
    }
    return result;
}
ActorArt ActorArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                        const rules::IniFile& rules, const ra2map::Infantry& actor, char theater) {
    return load(files, art, rules, actor.type, Kind::Infantry, theater);
}
ActorArt ActorArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                        const rules::IniFile& rules, const ra2map::Structure& actor, char theater) {
    return load(files, art, rules, actor.type, Kind::Building, theater);
}
ActorArt ActorArt::load(const fs::MixFileSystem& files, const rules::IniFile& art,
                        const rules::IniFile& rules, const ra2map::TerrainObject& actor, char theater) {
    return load(files, art, rules, actor.type, Kind::Terrain, theater);
}
ActorArt ActorArt::load_attached(const fs::MixFileSystem& files, const rules::IniFile& art,
                                 const rules::IniFile& rules, std::string_view type,
                                 std::string_view key, char theater) {
    const auto image = rules.read_string(type, "Image", type);
    // 0x0045f230 reads the named building animation art keys.
    const auto animation = art.read_string(image, key);
    if (animation.empty()) throw std::runtime_error("attached animation not found");
    return load(files, art, rules, animation, Kind::Effect, theater);
}
std::size_t ActorArt::frame(std::string_view sequence, unsigned direction, unsigned step) const {
    if (kind_ != Kind::Infantry) {
        if (!sequence.empty()) throw std::runtime_error("building and effect sequences are unsupported");
        if (step >= image_.header().frame_count) throw std::runtime_error("actor frame out of range");
        return step;
    }
    const auto it = sequences_.find(sequence);
    if (it == sequences_.end() || it->second.count == 0) throw std::runtime_error("infantry sequence unavailable");
    if (direction >= 8 || step >= static_cast<unsigned>(it->second.count)) throw std::runtime_error("infantry frame out of range");
    const auto index = std::uint64_t(it->second.first) + std::uint64_t(direction) * it->second.stride + step;
    if (index >= image_.header().frame_count) throw std::runtime_error("sequence exceeds SHP frames");
    return static_cast<std::size_t>(index);
}
std::size_t ActorArt::frame_for_map(std::string_view name, std::uint8_t map_facing,
                                    unsigned step) const {
    if (kind_ != Kind::Infantry) throw std::runtime_error("map facing supported only for infantry");
    const auto it = sequences_.find(name);
    if (it == sequences_.end() || it->second.count <= 0)
        throw std::runtime_error("infantry sequence unavailable");
    // 0x00518E18 wraps the sequence frame by its frame count.
    return frame(name, infantry_direction(map_facing), step % static_cast<unsigned>(it->second.count));
}
std::size_t ActorArt::frame_at_tick(std::string_view name, unsigned direction,
                                    std::uint64_t ticks, bool& finished, unsigned adjusted_delay) const {
    if (kind_ == Kind::Effect) {
        if (!name.empty() || !effect_timing_)
            throw std::runtime_error("unsupported timed effect");
        const auto& timing = *effect_timing_;
        const auto elapsed = ticks / static_cast<unsigned>(timing.delay);
        const auto first_length = static_cast<unsigned>(timing.loop_end - timing.start);
        const auto loop_length = static_cast<unsigned>(timing.loop_end - timing.loop_start);
        const auto index = elapsed < first_length ? timing.start + elapsed :
            timing.loop_start + (elapsed - first_length) % loop_length;
        finished = false;
        return static_cast<std::size_t>(index);
    }
    const auto it = sequences_.find(name);
    if (it == sequences_.end() || it->second.count <= 0 || it->second.delay < 0)
        throw std::runtime_error("unsupported timed sequence");
    unsigned delay = static_cast<unsigned>(it->second.delay);
    if (speed_scaled_delay(name)) {
        if (!adjusted_delay) throw std::runtime_error("game-speed-adjusted sequence delay required");
        delay = adjusted_delay;
    }
    // 0x006f9e50 increments once when the sequence timer expires.
    const auto elapsed = delay ? ticks / delay : 0;
    finished = delay && elapsed >= static_cast<unsigned>(it->second.count);
    const auto step = std::min<std::uint64_t>(elapsed, static_cast<unsigned>(it->second.count - 1));
    return frame(name, direction, static_cast<unsigned>(step));
}
ScreenPoint ActorArt::main_canvas_origin(ScreenPoint anchor) const noexcept {
    // 0x00705E00 sets flag 0x200 for the main draw; 0x004AED70
    // subtracts half the nominal SHP dimensions when that flag is set.
    return {anchor.x - image_.header().width / 2, anchor.y - image_.header().height / 2};
}
gfx::RgbaImage ActorArt::draw_frame(std::size_t index, const gfx::Palette& palette) const {
    return image_.decode_frame(index, palette);
}
gfx::RgbaImage ActorArt::draw(std::string_view sequence, unsigned direction, unsigned step, const gfx::Palette& palette) const {
    return image_.decode_frame(frame(sequence, direction, step), palette);
}
}
