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
// Original single-light lookup. The caller supplies light in section
// coordinates.
std::array<std::uint8_t, 256> single_light(unsigned normal_type,
                                           std::array<float, 3> direction);
struct Image {
  unsigned width, height;
  std::vector<ra2::gfx::Rgba> pixels;
};
// Experimental orthographic view. A supplied lookup selects per-normal
// lighting.
Image draw(const Model &, const Animation &, const Lighting &, unsigned frame,
           unsigned level, unsigned width, unsigned height, float angle,
           float zoom,
           const std::array<std::uint8_t, 256> *normal_light = nullptr);
} // namespace ra2::voxel
