#pragma once
#include "../gfx/palette.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace ra2::voxel {
using Bytes = std::vector<std::uint8_t>;
struct Point { std::uint8_t x,y,z,color,normal; };
struct Section { std::string name; std::uint8_t x,y,z,normal_type; float scale; std::array<float,3> min,max; std::vector<Point> points; };
struct Model { std::vector<Section> sections; };
struct Animation { std::uint32_t frames; std::vector<std::string> names; std::vector<std::array<float,12>> matrices; };
struct Lighting { ra2::gfx::Palette palette; std::vector<std::array<std::uint8_t,256>> tables; };
Model read_vxl(const Bytes&);
Animation read_hva(const Bytes&);
Lighting read_vpl(const Bytes&);
struct Image { unsigned width,height; std::vector<ra2::gfx::Rgba> pixels; };
// Experimental orthographic view. Lighting level is explicit; original normal lighting is unknown.
Image draw(const Model&, const Animation&, const Lighting&, unsigned frame, unsigned level, unsigned width, unsigned height, float angle, float zoom);
}
