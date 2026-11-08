#pragma once
#include "../gfx/palette.h"
#include <array>
#include <cstdint>
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
struct Image {
  unsigned width, height;
  std::vector<ra2::gfx::Rgba> pixels;
};
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
