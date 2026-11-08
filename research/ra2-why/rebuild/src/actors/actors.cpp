#include "actors.h"
#include <cstdio>
#include <array>
#include <algorithm>
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
    if (art.read_bool(image, "NewTheater", false) && theater_name) {
        if (theater_code < 'A' || theater_code > 'Z') throw std::runtime_error("invalid theater code");
        filename[1] = theater_code;
    }
    auto bytes = files.read_file(filename);
    if (!bytes && art.read_bool(image, "NewTheater", false) && theater_name) {
        // 0x005f9710 and 0x0045f230 retry the generic G variant.
        filename[1] = 'G';
        bytes = files.read_file(filename);
    }
    if (!bytes) throw std::runtime_error("actor SHP not found: " + filename);
    ActorArt result(gfx::ShpImage::parse(std::move(*bytes)), filename, kind,
                    art.read_bool(image, "Remapable", false));
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
std::size_t ActorArt::frame_at_tick(std::string_view name, unsigned direction,
                                    std::uint64_t ticks, bool& finished, unsigned adjusted_delay) const {
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
gfx::RgbaImage ActorArt::draw_frame(std::size_t index, const gfx::Palette& palette) const {
    return image_.decode_frame(index, palette);
}
gfx::RgbaImage ActorArt::draw(std::string_view sequence, unsigned direction, unsigned step, const gfx::Palette& palette) const {
    return image_.decode_frame(frame(sequence, direction, step), palette);
}
}
