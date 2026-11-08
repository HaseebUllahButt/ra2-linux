#pragma once
#include "../gfx/shp_decode.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../map/map_file.h"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace ra2::actors {
enum class Kind { Infantry, Building, Effect, Terrain };
struct ScreenPoint { std::int64_t x, y; };
struct WorldPosition { std::int64_t x, y, z; };
// Original building center in engine world units, before screen projection.
WorldPosition building_center(ra2map::Cell cell, unsigned foundation_width,
                              unsigned foundation_height, std::int32_t world_z);
// Converts the map facing byte to the original infantry SHP direction.
unsigned infantry_direction(std::uint8_t map_facing) noexcept;
struct Sequence { int first, count, stride; std::string fixed_facing; int delay; };
class ActorArt {
public:
    // The caller mounts the original MIX archives and selects the theater.
    static ActorArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                         const rules::IniFile& rules, std::string_view type,
                         Kind kind, char theater_code = 'T');
    static ActorArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                         const rules::IniFile& rules, const ra2map::Infantry& actor,
                         char theater_code = 'T');
    static ActorArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                         const rules::IniFile& rules, const ra2map::Structure& actor,
                         char theater_code = 'T');
    static ActorArt load(const fs::MixFileSystem& files, const rules::IniFile& art,
                         const rules::IniFile& rules, const ra2map::TerrainObject& actor,
                         char theater_code = 'T');
    // Resolve an art key such as ActiveAnim to its separate SHP animation.
    static ActorArt load_attached(const fs::MixFileSystem& files, const rules::IniFile& art,
                                  const rules::IniFile& rules, std::string_view type,
                                  std::string_view art_key, char theater_code = 'T');
    // direction is an already selected SHP facing, 0..7. step is the animation frame.
    std::size_t frame(std::string_view sequence, unsigned direction, unsigned step) const;
    std::size_t frame_for_map(std::string_view sequence, std::uint8_t map_facing,
                              unsigned step) const;
    // Returns the current frame within a single sequence; completion is reported.
    // Some sequence delays depend on game speed; the caller supplies that delay.
    std::size_t frame_at_tick(std::string_view sequence, unsigned direction,
                              std::uint64_t ticks, bool& finished,
                              unsigned adjusted_delay = 0) const;
    gfx::RgbaImage draw(std::string_view sequence, unsigned direction, unsigned step,
                        const gfx::Palette& palette) const;
    // Original centered main-image draw, after the caller computes the anchor.
    ScreenPoint main_canvas_origin(ScreenPoint anchor) const noexcept;
    gfx::RgbaImage draw_frame(std::size_t frame, const gfx::Palette& palette) const;
    const std::string& filename() const noexcept { return filename_; }
    const std::map<std::string, Sequence, std::less<>>& sequences() const noexcept { return sequences_; }
    bool remappable() const noexcept { return remappable_; }
private:
    ActorArt(gfx::ShpImage image, std::string filename, Kind kind, bool remappable);
    gfx::ShpImage image_;
    std::string filename_;
    Kind kind_;
    bool remappable_;
    std::map<std::string, Sequence, std::less<>> sequences_;
};
}
