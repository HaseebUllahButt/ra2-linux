#pragma once
#include "../gfx/palette.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace ra2::voxel {
using Bytes = std::vector<std::uint8_t>;
struct Point {
  std::uint8_t x, y, z, color, normal;
};
struct Section {
  std::string name;
  std::uint8_t x, y, z, normal_type;
  float scale;
  std::array<float, 12> transform;
  std::array<float, 3> min, max;
  std::vector<Point> points;
  std::vector<std::uint8_t> occupied_columns;
};
struct Model {
  std::vector<Section> sections;
};
struct Animation {
  std::uint32_t frames;
  std::vector<std::string> names;
  std::vector<std::array<float, 12>> matrices;
};
struct Lighting {
  std::uint32_t remap_first = 0, remap_last = 0;
  std::array<ra2::gfx::Rgba, 256> palette;
  std::vector<std::array<std::uint8_t, 256>> tables;
};
Model read_vxl(const Bytes &);
Animation read_hva(const Bytes &);
Lighting read_vpl(const Bytes &);
// Match 0x5BD730: multiply matrix translations by the first VXL section scale.
void scale_hva_translations(Animation &, const Model &);
// Replace only the VPL reserved range. Lighting tables keep that range
// separate.
Lighting make_default_lighting(const Lighting &);
Lighting team_colors(const Lighting &, const std::array<ra2::gfx::Rgba, 256> &);
std::array<std::uint8_t, 256> double_light(unsigned normal_type,
                                           std::array<float, 3> first,
                                           std::array<float, 3> second,
                                           float intensity);
// Original single-light lookup. The caller supplies light in section
// coordinates.
std::array<std::uint8_t, 256> single_light(unsigned normal_type,
                                           std::array<float, 3> direction);
// Ground coverage from 0x756860. Corners are in the order produced by
// 0x753F90; center is the projected bounds center. The result is 256x256.
using ProjectedCorner = std::array<float, 2>;
// Applies section and view matrices to the four ground bounds, as in 0x753F90.
std::array<ProjectedCorner, 4>
project_ground_corners(const Section &,
                       const std::array<float, 12> &section_matrix,
                       const std::array<float, 12> &view_matrix,
                       const std::array<float, 3> &offset);
std::vector<std::uint8_t>
ground_coverage(const Section &, const std::array<ProjectedCorner, 4> &corners,
                ProjectedCorner center);
// Indexed raster for the original non-depth path. Pass both lighting inputs
// to apply normal-indexed VPL colors, or neither for unlit colors.
std::vector<std::uint8_t>
raster_section(const Section &, const std::array<float, 12> &matrix,
               const Lighting *lighting = nullptr,
               const std::array<std::uint8_t, 256> *normal_light = nullptr);
struct DepthRaster {
  std::vector<std::uint8_t> pixels;
  std::vector<std::uint8_t> depths;
  int left = 0, top = 0;
};
// Base game raw-surface gradient record 2 (0x434C64, 0x434F33).
// Input coordinates and depth base are the original Z-buffer values.
struct BaseDepthRow {
  int value;
  int phase;
};
BaseDepthRow base_depth_row(int z_base, int z_origin_y, int destination_y,
                            int clipped_y, int clipped_height, int adjustment);
// Flat, clear ground only. projected_height is the original 0x6A2970 result
// for the vehicle's world Z. Slopes, bridges, nearby objects and other
// locomotors need the complete map checks at 0x4C9930 and 0x6CFEA0.
int base_flat_vehicle_depth_adjustment(int projected_height);
struct BaseFlatVehicleDepthInput {
  int z_base, z_origin_y, destination_y, clipped_y, clipped_height;
  int projected_height;
};
BaseDepthRow base_flat_vehicle_depth_row(const BaseFlatVehicleDepthInput &);
void base_depth_next_row(BaseDepthRow &);
// Original raw blitter 0x488870: transparent pixels and equal depths fail.
bool base_depth_visible(std::uint8_t pixel, int row_depth,
                        std::uint16_t terrain_depth);
// Original depth-enabled section raster. Both planes are 256 by 256.
DepthRaster raster_section_depth(
    const Section &, const std::array<float, 12> &matrix,
    const Lighting *lighting = nullptr,
    const std::array<std::uint8_t, 256> *normal_light = nullptr);
struct ShadowMask {
  int left, top;
  std::vector<std::uint8_t> pixels; // 256 by 256; nonzero marks shadow.
};
// Return true when the original surface pixel blocks a shadow.
using ShadowOcclusion = std::function<bool(int x, int y)>;
// surface_x/y are the original caller's surface offsets, before adding center.
ShadowMask base_shadow_section(const Section &,
                               const std::array<float, 12> &section_matrix,
                               const ShadowOcclusion &occluded = {},
                               int surface_x = 0, int surface_y = 0);
struct Image {
  unsigned width, height;
  std::vector<ra2::gfx::Rgba> pixels;
};
using Matrix = std::array<float, 12>;
// Section matrices are combined with each part matrix. Supply parts in draw
// order.
struct ScenePart {
  const Model *model = nullptr;
  const Animation *animation = nullptr;
  Matrix matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  unsigned frame = 0;
  std::array<std::uint8_t, 256> normal_light{};
};
struct SceneImage {
  Image image;
  int left, top;
  // Source rectangle on the original 256x256 part surface (0x7178E0).
  int source_x = 0, source_y = 0, source_width = 256, source_height = 256;
};
// Default game camera from the original initialization at 0x7558CE.
Matrix game_camera();
// Original base game.exe values, including its angle lookup precision.
Matrix base_game_camera();
// Original 32-step facing and X offset from 0x43E085 through 0x43E0E3.
Matrix facing_transform(std::uint16_t facing, std::int32_t offset);
Matrix base_facing_transform(std::uint16_t facing, std::int32_t offset);
// Base vehicle light selection for a complete part-to-view matrix.
std::array<std::uint8_t, 256> base_vehicle_light(const Matrix &,
                                                 unsigned normal_type);
// Pivot rotation sequence at 0x43E439 through 0x43E52D.
Matrix barrel_transform(Matrix base, std::uint16_t rotation,
                        std::uint16_t tilt);
// Multi-part alignment remains unverified against the game.
SceneImage draw_scene(const std::vector<ScenePart> &, const Lighting &);
// Animation must contain unscaled values from read_hva().
struct VehiclePart {
  const Model *model = nullptr;
  const Animation *animation = nullptr;
  unsigned frame = 0;
  std::array<std::uint8_t, 256> normal_light{};
};
struct VehiclePose {
  VehiclePart hull, turret, barrel;
  std::uint16_t hull_facing = 0, turret_facing = 0, barrel_tilt = 0x4000;
  std::int32_t turret_offset = 0;
  // Use the original base-game directional light when true.
  bool original_lighting = true;
};
// Base map placement initializes both facings from the map's 8-bit value.
VehiclePose vehicle_pose_from_map(std::uint8_t facing);
struct VehicleMatrices {
  Matrix hull, turret, barrel;
};
// Local part matrices before camera projection.
VehicleMatrices vehicle_matrices(const VehiclePose &);
// Flat, stationary vehicle. Part order and multi-part alignment remain
// provisional.
SceneImage draw_vehicle(const VehiclePose &, const Lighting &);
// Parts remain in original draw order. Each rectangle is relative to its own
// 256x256 surface. Use the separate rectangle for terrain depth clipping.
std::vector<SceneImage> draw_vehicle_parts(const VehiclePose &, const Lighting &);
struct FlatPartDepth {
  BaseDepthRow row{0, 0};
  int source_y = 0;
  int row_count = 0;
};
// Full-map terrain surface: original base 0x8000, origin Y 0. Clip the
// part vertically, and return the first source row and its original depth.
FlatPartDepth base_flat_part_depth(const SceneImage &, int anchor_y,
                                   int surface_height, int projected_height);
// Convert the original 256x256 lit indexed raster to RGBA.
Image raster_rgba(const Section &, const std::array<float, 12> &matrix,
                  const Lighting &,
                  const std::array<std::uint8_t, 256> &normal_light);
// Experimental orthographic view. A supplied lookup selects per-normal
// lighting.
Image draw(const Model &, const Animation &, const Lighting &, unsigned frame,
           unsigned level, unsigned width, unsigned height, float angle,
           float zoom,
           const std::array<std::uint8_t, 256> *normal_light = nullptr);
} // namespace ra2::voxel
