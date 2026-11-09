#pragma once
#include "../gfx/shp_decode.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../map/map_file.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ra2::actors {
enum class Kind { Infantry, Building, Effect, Terrain, Vehicle };
// Basic locomotion state; action, death, deployment, and combat states are separate.
// water means the original water-capable, water-cell, non-airborne branch applies.
enum class InfantryMotion { Standing, Walking };
std::string_view infantry_motion_sequence(InfantryMotion motion, bool prone, bool water) noexcept;
struct ScreenPoint { std::int64_t x, y; };
struct WorldPosition { std::int64_t x, y, z; };
// Original height projection for the fixed tactical geometry.
int projected_height(std::int32_t world_z) noexcept;
ScreenPoint project_world(WorldPosition position, ScreenPoint viewport);
// Supply an already projected Z value, for callers that have it.
ScreenPoint project_world(WorldPosition position, int height_pixels, ScreenPoint viewport);
// Original world-space object ordering key, not the per-pixel depth buffer.
std::int64_t object_y_sort(WorldPosition draw_position, int adjustment = 0);
// The original TurretAnimIsVoxel and Gate flags add +32 and -16.
std::int64_t building_y_sort(WorldPosition draw_position, bool turret_anim_is_voxel, bool gate);
std::int64_t building_y_sort(WorldPosition draw_position, const rules::IniFile& rules,
                             std::string_view type);
// Map placement of a stationary infantry unit; world_z is the resolved terrain height.
WorldPosition infantry_position(ra2map::Cell cell, unsigned subcell, std::int32_t world_z);
// Terrain constructor starts at cell center and clamps its initial zero Z
// against the original floor query (base 006E5850; expansion 0071E0D0).
WorldPosition terrain_position(ra2map::Cell cell, std::int32_t floor_world_height);
// Initial, non-animated terrain draw. Burning and animation states are separate.
std::size_t terrain_initial_frame(const rules::IniFile& rules, std::string_view type);
// Base world point used by the building draw caller before projection.
WorldPosition building_draw_position(ra2map::Cell cell, std::int32_t world_z);
// Original building center in engine world units, before screen projection.
WorldPosition building_center(ra2map::Cell cell, unsigned foundation_width,
                              unsigned foundation_height, std::int32_t world_z);
// Converts the map facing byte to the original infantry SHP direction.
unsigned infantry_direction(std::uint8_t map_facing) noexcept;
// Base UnitType SHP frame layout. Constructor 0070A600 sets Facings 8,
// WalkFrames 12 and the start frames to -1; ReadINI 0070ACEF..0070AF9C
// reads the frame keys from the art section and derives missing starts.
struct ShpVehicleFrames {
    int facings, walk_frames, firing_frames, standing_frames;
    int start_walk, start_stand, start_firing;
};
ShpVehicleFrames shp_vehicle_frames(const rules::IniFile& rules, const rules::IniFile& art,
                                    std::string_view type);
// Base SHP unit draw 007005B0: eight-facing types use the high byte of the
// current body facing; other facing counts always use direction 0.
unsigned shp_vehicle_direction(std::uint8_t map_facing, int facings) noexcept;
// Stand frame of a resting unit, or a walk frame while the locomotor moves.
// Firing, death and idle-animation frames are separate states.
std::size_t shp_vehicle_frame(const ShpVehicleFrames& frames, std::uint8_t map_facing,
                              bool moving, unsigned walk_counter);
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
    struct EffectTiming { int start, loop_start, loop_end, delay; };
    std::optional<EffectTiming> effect_timing_;
    bool remappable_;
    std::map<std::string, Sequence, std::less<>> sequences_;
};
}
