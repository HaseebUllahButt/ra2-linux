#include "voxel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace ra2::voxel {
namespace {
#include "base_facing.inc"
#include "base_tilt.inc"
#include "normal_vectors.inc"
void check(const Bytes &b, std::size_t p, std::size_t n) {
  if (p > b.size() || n > b.size() - p)
    throw std::runtime_error("truncated voxel file");
}
std::uint32_t u32(const Bytes &b, std::size_t p) {
  check(b, p, 4);
  return std::uint32_t(b[p]) | std::uint32_t(b[p + 1]) << 8 |
         std::uint32_t(b[p + 2]) << 16 | std::uint32_t(b[p + 3]) << 24;
}
float f32(const Bytes &b, std::size_t p) {
  auto v = u32(b, p);
  float f;
  std::memcpy(&f, &v, 4);
  if (!std::isfinite(f))
    throw std::runtime_error("invalid float");
  return f;
}
std::string name(const Bytes &b, std::size_t p) {
  check(b, p, 16);
  std::size_t n = 0;
  while (n < 16 && b[p + n])
    ++n;
  return std::string(reinterpret_cast<const char *>(b.data() + p), n);
}
} // namespace
Model read_vxl(const Bytes &b) {
  check(b, 0, 32);
  if (std::memcmp(b.data(), "Voxel Animation", 15))
    throw std::runtime_error("invalid VXL signature");
  auto palettes = u32(b, 16), heads = u32(b, 20), tails = u32(b, 24),
       body_size = u32(b, 28);
  if (!heads || heads != tails || heads > 1024 || palettes > 256)
    throw std::runtime_error("invalid VXL counts");
  std::size_t header = 32 + std::size_t(palettes) * 770;
  check(b, header, std::size_t(heads) * 28);
  std::size_t body = header + std::size_t(heads) * 28;
  check(b, body, body_size);
  std::size_t tail = body + body_size;
  check(b, tail, std::size_t(tails) * 92);
  Model m;
  std::size_t total_points = 0;
  for (std::size_t s = 0; s < heads; ++s) {
    auto h = header + 28 * s, t = tail + 92 * s;
    Section section{};
    section.name = name(b, h);
    if (section.name.empty() ||
        std::any_of(
            m.sections.begin(), m.sections.end(),
            [&](const Section &old) { return old.name == section.name; }))
      throw std::runtime_error("invalid VXL section name");
    section.scale = f32(b, t + 12);
    for (std::size_t i = 0; i < section.transform.size(); ++i)
      section.transform[i] = f32(b, t + 16 + i * 4);
    for (int i = 0; i < 3; ++i) {
      section.min[i] = f32(b, t + 64 + 4 * i);
      section.max[i] = f32(b, t + 76 + 4 * i);
    }
    section.x = b[t + 88];
    section.y = b[t + 89];
    section.z = b[t + 90];
    section.normal_type = b[t + 91];
    if (!section.x || !section.y || !section.z)
      throw std::runtime_error("invalid VXL dimensions");
    if (section.normal_type != 2 && section.normal_type != 4)
      throw std::runtime_error("unsupported VXL normal type");
    for (unsigned axis = 0; axis < 3; ++axis)
      if (!(section.min[axis] < section.max[axis]))
        throw std::runtime_error("invalid VXL bounds");
    std::size_t count = std::size_t(section.x) * section.y;
    section.occupied_columns.resize(count);
    auto starts = u32(b, t), ends = u32(b, t + 4), data = u32(b, t + 8);
    if (starts > body_size || ends > body_size || data > body_size ||
        count * 4 > body_size - starts || count * 4 > body_size - ends)
      throw std::runtime_error("invalid VXL tables");
    for (std::size_t i = 0; i < count; ++i) {
      auto a = u32(b, body + starts + i * 4), z = u32(b, body + ends + i * 4);
      if (a == 0xffffffff && z == 0xffffffff)
        continue;
      section.occupied_columns[i] = 1;
      if (a > z || z >= body_size - data)
        throw std::runtime_error("invalid VXL span");
      std::size_t p = body + data + a, end = body + data + z + 1;
      unsigned height = 0;
      while (p < end) {
        if (end - p < 2)
          throw std::runtime_error("truncated VXL span");
        height += b[p++];
        unsigned n = b[p++];
        if (height + n > section.z || end - p < std::size_t(n) * 2 + 1)
          throw std::runtime_error("invalid VXL run");
        for (unsigned j = 0; j < n; ++j) {
          if (b[p + 1] >= (section.normal_type == 2 ? 36 : 245) &&
              b[p + 1] < 253)
            throw std::runtime_error("invalid VXL normal index " +
                                     std::to_string(b[p + 1]) + " type " +
                                     std::to_string(section.normal_type));
          if (++total_points > 16000000)
            throw std::runtime_error("VXL point limit exceeded");
          section.points.push_back({std::uint8_t(i % section.x),
                                    std::uint8_t(i / section.x),
                                    std::uint8_t(height++), b[p], b[p + 1]});
          p += 2;
        }
        if (b[p++] != n)
          throw std::runtime_error("invalid VXL run end");
      }
    }
    m.sections.push_back(std::move(section));
  }
  return m;
}
Animation read_hva(const Bytes &b) {
  check(b, 0, 24);
  Animation a{};
  a.frames = u32(b, 16);
  auto sections = u32(b, 20);
  if (!a.frames || !sections || sections > 1024 || a.frames > 100000 ||
      std::uint64_t(a.frames) * sections > 1000000)
    throw std::runtime_error("invalid HVA counts");
  std::size_t p = 24;
  check(b, p, std::size_t(sections) * 16);
  for (unsigned i = 0; i < sections; ++i) {
    auto section_name = name(b, p + i * 16);
    if (section_name.empty() || std::find(a.names.begin(), a.names.end(),
                                          section_name) != a.names.end())
      throw std::runtime_error("invalid HVA section name");
    a.names.push_back(std::move(section_name));
  }
  p += std::size_t(sections) * 16;
  check(b, p, std::size_t(a.frames) * sections * 48);
  a.matrices.resize(std::size_t(a.frames) * sections);
  for (auto &mat : a.matrices)
    for (auto &x : mat) {
      x = f32(b, p);
      p += 4;
    }
  return a;
}
Lighting read_vpl(const Bytes &b) {
  check(b, 0, 784);
  auto levels = u32(b, 8);
  if (!levels || levels > 128)
    throw std::runtime_error("invalid VPL count");
  check(b, 784, std::size_t(levels) * 256);
  Lighting l{};
  l.remap_first = u32(b, 0);
  l.remap_last = u32(b, 4);
  if (l.remap_first > l.remap_last || l.remap_last > 255)
    throw std::runtime_error("invalid VPL remap range");
  for (std::size_t i = 0; i < 256; ++i)
    l.palette[i] = {b[16 + i * 3], b[17 + i * 3], b[18 + i * 3], 255};
  l.tables.resize(levels);
  for (unsigned i = 0; i < levels; ++i)
    std::copy_n(b.begin() + 784 + std::size_t(i) * 256, 256,
                l.tables[i].begin());
  return l;
}
void scale_hva_translations(Animation &animation, const Model &model) {
  if (model.sections.empty() || !std::isfinite(model.sections.front().scale))
    throw std::runtime_error("invalid VXL scale");
  if (std::uint64_t(animation.frames) * animation.names.size() !=
      animation.matrices.size())
    throw std::runtime_error("invalid HVA matrix count");
  const float scale = model.sections.front().scale;
  for (auto &matrix : animation.matrices) {
    for (unsigned i : {3u, 7u, 11u}) {
      if (!std::isfinite(matrix[i]) || !std::isfinite(matrix[i] * scale))
        throw std::runtime_error("invalid HVA translation");
    }
  }
  for (auto &matrix : animation.matrices)
    for (unsigned i : {3u, 7u, 11u})
      matrix[i] *= scale;
}
Lighting make_default_lighting(const Lighting &lighting) {
  if (lighting.remap_first < 1 || lighting.remap_first > lighting.remap_last ||
      lighting.remap_last > 255)
    throw std::runtime_error("invalid VPL remap range");
  Lighting result = lighting;
  result.tables.assign(32, {});
  // 0x758B70 applies these brightness factors. 0x758EA0 searches only
  // within the source color's reserved or non-reserved range.
  for (unsigned level = 0; level < 32; ++level) {
    float factor = level < 16 ? 0.6f + float(level) * 0.0625f * 0.8f
                              : 0.4f + float(2 * level - 16) * 0.0625f;
    for (unsigned index = 1; index < 256; ++index) {
      const auto source = lighting.palette[index];
      const bool remap =
          index >= lighting.remap_first && index <= lighting.remap_last;
      const float r =
          level < 16 ? source.r * factor : std::min(255.0f, source.r * factor);
      const float g =
          level < 16 ? source.g * factor : std::min(255.0f, source.g * factor);
      const float b =
          level < 16 ? source.b * factor : std::min(255.0f, source.b * factor);
      float best = 100000.0f;
      unsigned nearest = remap ? lighting.remap_first : 1;
      for (unsigned candidate = 1; candidate < 256; ++candidate) {
        bool candidate_remap = candidate >= lighting.remap_first &&
                               candidate <= lighting.remap_last;
        if (candidate_remap != remap)
          continue;
        auto color = lighting.palette[candidate];
        float dr = r - color.r, dg = g - color.g, db = b - color.b;
        float distance = dr * dr + dg * dg + db * db;
        if (distance < best) {
          best = distance;
          nearest = candidate;
        }
      }
      result.tables[level][index] = static_cast<std::uint8_t>(nearest);
    }
  }
  return result;
}
Lighting team_colors(const Lighting &lighting,
                     const std::array<ra2::gfx::Rgba, 256> &colors) {
  if (lighting.remap_first > lighting.remap_last || lighting.remap_last > 255)
    throw std::runtime_error("invalid VPL remap range");
  auto result = lighting;
  for (unsigned i = lighting.remap_first; i <= lighting.remap_last; ++i)
    result.palette[i] = colors[i];
  return result;
}
std::array<std::uint8_t, 256> double_light(unsigned normal_type,
                                           std::array<float, 3> first,
                                           std::array<float, 3> second,
                                           float intensity) {
  if ((normal_type != 2 && normal_type != 4) || !std::isfinite(intensity) ||
      intensity <= 0)
    throw std::runtime_error("invalid double light");
  std::array<float, 3> halfway{};
  float length_squared = 0;
  for (unsigned axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(first[axis]) || !std::isfinite(second[axis]))
      throw std::runtime_error("invalid light direction");
    halfway[axis] = first[axis] + second[axis];
    length_squared += halfway[axis] * halfway[axis];
  }
  if (!std::isfinite(length_squared))
    throw std::runtime_error("invalid light direction");
  if (length_squared > 0) {
    float length = std::sqrt(length_squared);
    for (auto &component : halfway)
      component /= length;
  }
  std::array<std::uint8_t, 256> result{};
  const auto *vectors = normal_type == 2 ? normal2.data() : normal4.data();
  unsigned count = normal_type == 2 ? normal2.size() : normal4.size();
  for (unsigned i = 0; i < count; ++i) {
    float diffuse = 0, specular = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
      diffuse += vectors[i][axis] * first[axis];
      specular += vectors[i][axis] * halfway[axis];
    }
    float denominator = intensity - specular * intensity + specular;
    float highlight = denominator != 0 ? specular / denominator : 0;
    float value = std::max(0.0f, diffuse) + std::max(0.0f, highlight);
    result[i] = static_cast<std::uint8_t>(
        std::clamp(std::trunc(value * 16), 0.0f, 31.0f));
  }
  result[253] = result[254] = result[255] = 16;
  return result;
}
std::array<std::uint8_t, 256> single_light(unsigned normal_type,
                                           std::array<float, 3> direction) {
  for (float component : direction)
    if (!std::isfinite(component))
      throw std::runtime_error("invalid light direction");
  if (normal_type != 2 && normal_type != 4)
    throw std::runtime_error("unsupported normal type");
  std::array<std::uint8_t, 256> result{};
  const auto *vectors = normal_type == 2 ? normal2.data() : normal4.data();
  unsigned count = normal_type == 2 ? normal2.size() : normal4.size();
  for (unsigned i = 0; i < count; ++i) {
    float dot = 0;
    for (unsigned axis = 0; axis < 3; ++axis)
      dot += vectors[i][axis] * direction[axis];
    // 0x758670 clamps negative values, multiplies by 16 and truncates.
    result[i] = static_cast<std::uint8_t>(
        std::clamp(std::trunc(dot * 16), 0.0f, 31.0f));
  }
  result[253] = result[254] = result[255] = 16;
  return result;
}
std::array<ProjectedCorner, 4>
project_ground_corners(const Section &section,
                       const std::array<float, 12> &section_matrix,
                       const std::array<float, 12> &view_matrix,
                       const std::array<float, 3> &offset) {
  const std::array<std::array<float, 3>, 4> corners = {
      {{section.max[0], section.max[1], section.min[2]},
       {section.max[0], section.min[1], section.min[2]},
       {section.min[0], section.min[1], section.min[2]},
       {section.min[0], section.max[1], section.min[2]}}};
  auto transform = [](const std::array<float, 12> &matrix,
                      const std::array<float, 3> &point) {
    std::array<float, 3> result{};
    for (unsigned row = 0; row < 3; ++row)
      result[row] = matrix[row * 4] * point[0] +
                    matrix[row * 4 + 1] * point[1] +
                    matrix[row * 4 + 2] * point[2] + matrix[row * 4 + 3];
    return result;
  };
  std::array<ProjectedCorner, 4> result{};
  for (unsigned i = 0; i < 4; ++i) {
    auto point = transform(section_matrix, corners[i]);
    point[2] = 0;
    point = transform(view_matrix, point);
    result[i] = {point[0] + offset[0], -(point[1] + offset[1])};
    if (!std::isfinite(result[i][0]) || !std::isfinite(result[i][1]))
      throw std::runtime_error("invalid projected corner");
  }
  return result;
}
static std::vector<std::uint8_t>
ground_coverage_impl(const Section &section,
                     const std::array<ProjectedCorner, 4> &corners,
                     ProjectedCorner center, const ShadowOcclusion &occluded,
                     int surface_x, int surface_y) {
  if (!section.x || !section.y)
    throw std::runtime_error("invalid VXL dimensions");
  for (const auto &corner : corners)
    for (float coordinate : corner)
      if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
        throw std::runtime_error("invalid projected corner");
  for (float coordinate : center)
    if (!std::isfinite(coordinate) || std::abs(coordinate) > 1000000)
      throw std::runtime_error("invalid projected center");
  std::vector<bool> occupied(std::size_t(section.x) * section.y);
  if (!section.occupied_columns.empty()) {
    if (section.occupied_columns.size() != occupied.size())
      throw std::runtime_error("invalid VXL column count");
    for (std::size_t i = 0; i < occupied.size(); ++i)
      occupied[i] = section.occupied_columns[i] != 0;
  }
  for (const auto &point : section.points) {
    if (point.x >= section.x || point.y >= section.y)
      throw std::runtime_error("point outside VXL section");
    occupied[std::size_t(point.y) * section.x + point.x] = true;
  }
  auto fixed = [](double value) {
    return static_cast<std::uint16_t>(
        static_cast<std::int64_t>(std::trunc(value * 256)));
  };
  auto x0 = fixed(corners[2][0] + 128.0 - center[0]);
  auto y0 = fixed(corners[2][1] + 128.0 - center[1]);
  const auto dx_x = fixed((corners[1][0] - corners[2][0]) / section.x);
  const auto dx_y = fixed((corners[1][1] - corners[2][1]) / section.x);
  const auto dy_x = fixed((corners[3][0] - corners[2][0]) / section.y);
  const auto dy_y = fixed((corners[3][1] - corners[2][1]) / section.y);
  std::vector<std::uint8_t> result(65536);
  for (unsigned y = 0; y < section.y; ++y) {
    auto x_fixed = x0, y_fixed = y0;
    for (unsigned x = 0; x < section.x; ++x) {
      if (occupied[std::size_t(y) * section.x + x]) {
        auto offset = std::size_t(y_fixed >> 8) * 256 + (x_fixed >> 8);
        auto sample_x = std::uint16_t(
            std::int64_t(std::trunc(double(surface_x) + center[0])) +
            (x_fixed >> 8));
        auto sample_y = std::uint16_t(
            std::int64_t(std::trunc(double(surface_y) + center[1])) +
            (y_fixed >> 8));
        result[offset] = !occluded || !occluded(sample_x, sample_y);
        if (offset + 1 < result.size())
          result[offset + 1] =
              !occluded || !occluded(int(sample_x) + 1, sample_y);
      }
      x_fixed = static_cast<std::uint16_t>(x_fixed + dx_x);
      y_fixed = static_cast<std::uint16_t>(y_fixed + dx_y);
    }
    x0 = static_cast<std::uint16_t>(x0 + dy_x);
    y0 = static_cast<std::uint16_t>(y0 + dy_y);
  }
  return result;
}
std::vector<std::uint8_t>
ground_coverage(const Section &section,
                const std::array<ProjectedCorner, 4> &corners,
                ProjectedCorner center) {
  return ground_coverage_impl(section, corners, center, {}, 0, 0);
}
static std::vector<std::uint8_t>
raster_section_impl(const Section &section, const std::array<float, 12> &matrix,
                    const Lighting *lighting,
                    const std::array<std::uint8_t, 256> *normal_light,
                    const std::array<float, 3> *shared_center,
                    const std::vector<std::uint8_t> *previous) {
  if ((lighting == nullptr) != (normal_light == nullptr))
    throw std::runtime_error("incomplete raster lighting");
  if (!section.x || !section.y || !section.z)
    throw std::runtime_error("invalid section dimensions");
  std::array<std::array<float, 3>, 8> corners{};
  for (unsigned i = 0; i < 8; ++i) {
    const float x = (i == 0 || i == 1 || i == 4 || i == 5) ? section.max[0]
                                                           : section.min[0];
    const float y = (i == 0 || i == 3 || i == 4 || i == 7) ? section.max[1]
                                                           : section.min[1];
    const float z = i < 4 ? section.min[2] : section.max[2];
    for (unsigned row = 0; row < 3; ++row) {
      corners[i][row] = matrix[row * 4] * x + matrix[row * 4 + 1] * y +
                        matrix[row * 4 + 2] * z + matrix[row * 4 + 3];
      if (!std::isfinite(corners[i][row]) || std::abs(corners[i][row]) > 100000)
        throw std::runtime_error("invalid raster transform");
    }
    corners[i][1] = -corners[i][1];
  }
  std::array<float, 3> center{};
  unsigned nearest = 0;
  for (unsigned axis = 0; axis < 3; ++axis) {
    float lo = corners[0][axis], hi = lo;
    for (auto &corner : corners) {
      lo = std::min(lo, corner[axis]);
      hi = std::max(hi, corner[axis]);
    }
    center[axis] = (lo + hi) * 0.5f;
  }
  if (shared_center)
    center = *shared_center;
  for (unsigned i = 1; i < 8; ++i)
    if (corners[i][2] < corners[nearest][2])
      nearest = i;
  static constexpr int order[8][6] = {{0, 3, 1, 4, 1, 1}, {1, 2, 0, 5, 1, 0},
                                      {2, 1, 3, 6, 0, 0}, {3, 0, 2, 7, 0, 1},
                                      {4, 7, 5, 0, 1, 1}, {5, 6, 4, 1, 1, 0},
                                      {6, 5, 7, 2, 0, 0}, {7, 4, 6, 3, 0, 1}};
  static constexpr int step_x[4] = {-1, -1, 1, 1};
  static constexpr int step_y[4] = {-1, 1, 1, -1};
  const auto &v = order[nearest];
  auto fixed = [](double n) {
    return std::uint16_t(std::int64_t(std::trunc(n * 256)));
  };
  auto pack = [](std::uint16_t x, std::uint16_t y) {
    return std::uint32_t(x) | (std::uint32_t(y) << 16);
  };
  auto delta = [&](unsigned other, unsigned dim) {
    return pack(fixed((double(corners[other][0]) - corners[v[0]][0]) / dim),
                fixed((double(corners[other][1]) - corners[v[0]][1]) / dim));
  };
  auto position = pack(fixed(double(corners[v[0]][0]) + 128 - center[0]),
                       fixed(double(corners[v[0]][1]) + 128 - center[1]));
  auto inc_x = delta(v[1], section.x), inc_y = delta(v[2], section.y),
       inc_z = delta(v[3], section.z);
  std::vector<std::vector<Point>> columns(std::size_t(section.x) * section.y);
  for (auto point : section.points) {
    if (point.x >= section.x || point.y >= section.y || point.z >= section.z)
      throw std::runtime_error("invalid voxel point");
    columns[std::size_t(point.y) * section.x + point.x].push_back(point);
  }
  std::vector<std::uint8_t> result =
      previous ? *previous : std::vector<std::uint8_t>(65536);
  int start_x = v[4] ? section.x - 1 : 0, start_y = v[5] ? section.y - 1 : 0;
  for (unsigned y = 0; y < section.y; ++y) {
    auto cursor = position;
    for (unsigned x = 0; x < section.x; ++x) {
      auto &points =
          columns[std::size_t(start_y + int(y) * step_y[nearest % 4]) *
                      section.x +
                  start_x + int(x) * step_x[nearest % 4]];
      if (nearest >= 4)
        std::reverse(points.begin(), points.end());
      unsigned height = 0;
      auto p = cursor;
      for (auto point : points) {
        if (nearest >= 4)
          point.z = section.z - 1 - point.z;
        if (point.z < height)
          throw std::runtime_error("unsorted voxel points");
        auto skip = unsigned(point.z) - height;
        p += pack(std::uint16_t(std::uint16_t(inc_z) * skip),
                  std::uint16_t(std::uint16_t(inc_z >> 16) * skip));
        auto offset = std::size_t((p >> 24) & 255) * 256 + ((p >> 8) & 255);
        auto color = point.color;
        if (lighting) {
          const auto level = (*normal_light)[point.normal];
          if (level >= lighting->tables.size())
            throw std::runtime_error("light level outside VPL tables");
          color = lighting->tables[level][point.color];
        }
        result[offset] = color;
        p += inc_z;
        height = unsigned(point.z) + 1;
      }
      cursor += inc_x;
    }
    position += inc_y;
  }
  return result;
}
DepthRaster
raster_section_depth(const Section &section,
                     const std::array<float, 12> &matrix,
                     const Lighting *lighting,
                     const std::array<std::uint8_t, 256> *normal_light) {
  if ((lighting == nullptr) != (normal_light == nullptr) || !section.x ||
      !section.y || !section.z)
    throw std::runtime_error("invalid depth raster input");
  std::array<std::array<float, 3>, 8> corners{};
  for (unsigned i = 0; i < 8; ++i) {
    float x = (i == 0 || i == 1 || i == 4 || i == 5) ? section.max[0]
                                                     : section.min[0];
    float y = (i == 0 || i == 3 || i == 4 || i == 7) ? section.max[1]
                                                     : section.min[1];
    float z = i < 4 ? section.min[2] : section.max[2];
    for (unsigned row = 0; row < 3; ++row) {
      corners[i][row] = matrix[row * 4] * x + matrix[row * 4 + 1] * y +
                        matrix[row * 4 + 2] * z + matrix[row * 4 + 3];
      if (!std::isfinite(corners[i][row]) || std::abs(corners[i][row]) > 100000)
        throw std::runtime_error("invalid depth raster matrix");
    }
    corners[i][1] = -corners[i][1];
  }
  std::array<float, 3> center{};
  unsigned nearest = 0;
  for (unsigned axis = 0; axis < 3; ++axis) {
    float lo = corners[0][axis], hi = lo;
    for (auto &corner : corners) {
      lo = std::min(lo, corner[axis]);
      hi = std::max(hi, corner[axis]);
    }
    center[axis] = (lo + hi) * 0.5f;
  }
  for (unsigned i = 1; i < 8; ++i)
    if (corners[i][2] < corners[nearest][2])
      nearest = i;
  static constexpr int order[8][6] = {{0, 3, 1, 4, 1, 1}, {1, 2, 0, 5, 1, 0},
                                      {2, 1, 3, 6, 0, 0}, {3, 0, 2, 7, 0, 1},
                                      {4, 7, 5, 0, 1, 1}, {5, 6, 4, 1, 1, 0},
                                      {6, 5, 7, 2, 0, 0}, {7, 4, 6, 3, 0, 1}};
  static constexpr int step_x[4] = {-1, -1, 1, 1}, step_y[4] = {-1, 1, 1, -1};
  const auto &v = order[nearest];
  auto fixed = [](double n) {
    return std::uint16_t(std::int64_t(std::trunc(n * 256)));
  };
  std::array<std::uint16_t, 3> position{}, dx{}, dy{}, dz{};
  for (unsigned axis = 0; axis < 3; ++axis) {
    position[axis] = fixed(double(corners[v[0]][axis]) + 128 - center[axis]);
    dx[axis] =
        fixed((double(corners[v[1]][axis]) - corners[v[0]][axis]) / section.x);
    dy[axis] =
        fixed((double(corners[v[2]][axis]) - corners[v[0]][axis]) / section.y);
    dz[axis] =
        fixed((double(corners[v[3]][axis]) - corners[v[0]][axis]) / section.z);
  }
  std::vector<std::vector<Point>> columns(std::size_t(section.x) * section.y);
  for (auto point : section.points) {
    if (point.x >= section.x || point.y >= section.y || point.z >= section.z)
      throw std::runtime_error("invalid voxel point");
    columns[std::size_t(point.y) * section.x + point.x].push_back(point);
  }
  DepthRaster result{
      std::vector<std::uint8_t>(65536), std::vector<std::uint8_t>(65536),
      int(std::trunc(center[0])) - 128, int(std::trunc(center[1])) - 128};
  int start_x = v[4] ? section.x - 1 : 0, start_y = v[5] ? section.y - 1 : 0;
  for (unsigned y = 0; y < section.y; ++y) {
    auto cursor = position;
    for (unsigned x = 0; x < section.x; ++x) {
      auto &points =
          columns[std::size_t(start_y + int(y) * step_y[nearest % 4]) *
                      section.x +
                  start_x + int(x) * step_x[nearest % 4]];
      if (nearest >= 4)
        std::reverse(points.begin(), points.end());
      auto p = cursor;
      unsigned height = 0;
      for (auto point : points) {
        if (nearest >= 4)
          point.z = section.z - 1 - point.z;
        if (point.z < height)
          throw std::runtime_error("unsorted voxel points");
        unsigned skip = point.z - height;
        for (unsigned axis = 0; axis < 3; ++axis)
          p[axis] = std::uint16_t(p[axis] + dz[axis] * skip);
        auto offset = std::size_t(p[1] >> 8) * 256 + (p[0] >> 8);
        auto depth = std::uint8_t(p[2] >> 8);
        if (depth > result.depths[offset]) {
          auto color = point.color;
          if (lighting) {
            unsigned level = (*normal_light)[point.normal];
            if (level >= lighting->tables.size())
              throw std::runtime_error("light level outside VPL tables");
            color = lighting->tables[level][color];
          }
          result.pixels[offset] = color;
          result.depths[offset] = depth;
          if (offset + 1 < result.pixels.size()) {
            result.pixels[offset + 1] = color;
            result.depths[offset + 1] = depth;
          }
        }
        for (unsigned axis = 0; axis < 3; ++axis)
          p[axis] = std::uint16_t(p[axis] + dz[axis]);
        height = unsigned(point.z) + 1;
      }
      for (unsigned axis = 0; axis < 3; ++axis)
        cursor[axis] = std::uint16_t(cursor[axis] + dx[axis]);
    }
    for (unsigned axis = 0; axis < 3; ++axis)
      position[axis] = std::uint16_t(position[axis] + dy[axis]);
  }
  return result;
}

ShadowMask base_shadow_section(const Section &section,
                               const std::array<float, 12> &section_matrix,
                               const ShadowOcclusion &occluded, int surface_x,
                               int surface_y) {
  auto corners = project_ground_corners(
      section, section_matrix, base_game_camera(), {2.9999964237213135f, 0, 0});
  ProjectedCorner minimum = corners[0], maximum = corners[0];
  for (const auto &corner : corners)
    for (unsigned axis = 0; axis < 2; ++axis) {
      minimum[axis] = std::min(minimum[axis], corner[axis]);
      maximum[axis] = std::max(maximum[axis], corner[axis]);
    }
  if (maximum[0] - minimum[0] > 254 || maximum[1] - minimum[1] > 254)
    throw std::runtime_error("shadow exceeds voxel surface");
  ProjectedCorner center{(minimum[0] + maximum[0]) * 0.5f,
                         (minimum[1] + maximum[1]) * 0.5f};
  return {int(std::trunc(center[0])) - 128, int(std::trunc(center[1])) - 128,
          ground_coverage_impl(section, corners, center, occluded, surface_x,
                               surface_y)};
}

std::vector<std::uint8_t>
raster_section(const Section &section, const std::array<float, 12> &matrix,
               const Lighting *lighting,
               const std::array<std::uint8_t, 256> *normal_light) {
  return raster_section_impl(section, matrix, lighting, normal_light, nullptr,
                             nullptr);
}
Matrix game_camera() {
  // 0x7558CE applies RotateX(-60 degrees), then RotateZ(-45 degrees).
  constexpr float c = 0.7071067811865475f;
  constexpr float s = 0.8660254037844386f;
  return {c, c, 0, 0, -0.5f * c, 0.5f * c, s, 0, s * c, -s * c, 0.5f, 0};
}

Matrix base_game_camera() {
  return {0.7081906199455261f,
          0.706021249294281f,
          0,
          0,
          -0.3531669080257416f,
          0.35425207018852234f,
          0.865897536277771f,
          0,
          0.6113420128822327f,
          -0.6132205128669739f,
          0.5002213716506958f,
          0};
}
Matrix base_facing_transform(std::uint16_t facing, std::int32_t offset) {
  unsigned direction = (((unsigned(facing) >> 10) + 1) >> 1) & 31;
  float c = base_facing[direction][0], s = base_facing[direction][1];
  float distance = float(offset / 8);
  return {c, -s, 0, c * distance, s, c, 0, s * distance, 0, 0, 1, 0};
}
std::array<std::uint8_t, 256> base_vehicle_light(const Matrix &matrix,
                                                 unsigned normal_type) {
  constexpr std::array<float, 3> light = {
      -0.49999943375587463f, -0.7071059942245483f, 0.49999943375587463f};
  std::array<float, 3> local{};
  for (unsigned axis = 0; axis < 3; ++axis) {
    for (unsigned row = 0; row < 3; ++row)
      local[axis] += matrix[row * 4 + axis] * light[row];
  }
  return double_light(normal_type, local, {0, 0, 1}, 3);
}
Matrix facing_transform(std::uint16_t facing, std::int32_t offset) {
  unsigned direction = (((unsigned(facing) >> 10) + 1) >> 1) & 31;
  float angle = float((int(direction) - 8) * -0.19634954084936207);
  float c = std::cos(angle), s = std::sin(angle);
  float distance = float(offset / 8);
  return {c, -s, 0, c * distance, s, c, 0, s * distance, 0, 0, 1, 0};
}

Matrix barrel_transform(Matrix base, std::uint16_t rotation,
                        std::uint16_t tilt) {
  for (float value : base)
    if (!std::isfinite(value))
      throw std::runtime_error("invalid barrel matrix");
  auto direction_angle = [](std::uint16_t value) {
    unsigned step = (((unsigned(value) >> 10) + 1) >> 1) & 31;
    return float((int(step) - 8) * -0.19634954084936207);
  };
  const std::array<float, 3> pivot{base[3], base[7], base[11]};
  auto translate = [&](float sign) {
    for (unsigned row = 0; row < 3; ++row)
      base[row * 4 + 3] +=
          sign * (base[row * 4] * pivot[0] + base[row * 4 + 1] * pivot[1] +
                  base[row * 4 + 2] * pivot[2]);
  };
  translate(-1);
  float az = direction_angle(rotation), cz = std::cos(az), sz = std::sin(az);
  for (unsigned row = 0; row < 3; ++row) {
    float a = base[row * 4], b = base[row * 4 + 1];
    base[row * 4] = a * cz + b * sz;
    base[row * 4 + 1] = b * cz - a * sz;
  }
  float ay = -direction_angle(tilt), cy = std::cos(ay), sy = std::sin(ay);
  for (unsigned row = 0; row < 3; ++row) {
    float a = base[row * 4], b = base[row * 4 + 2];
    base[row * 4] = a * cy - b * sy;
    base[row * 4 + 2] = a * sy + b * cy;
  }
  translate(1);
  return base;
}

static Matrix base_barrel_transform(Matrix base, std::uint16_t rotation,
                                    std::uint16_t tilt) {
  for (float value : base)
    if (!std::isfinite(value))
      throw std::runtime_error("invalid barrel matrix");
  auto direction = [](std::uint16_t value) {
    return (((unsigned(value) >> 10) + 1) >> 1) & 31;
  };
  const std::array<float, 3> pivot{base[3], base[7], base[11]};
  auto translate = [&](float sign) {
    for (unsigned row = 0; row < 3; ++row)
      base[row * 4 + 3] +=
          sign * (base[row * 4] * pivot[0] + base[row * 4 + 1] * pivot[1] +
                  base[row * 4 + 2] * pivot[2]);
  };
  translate(-1);
  float cz = base_facing[direction(rotation)][0];
  float sz = base_facing[direction(rotation)][1];
  for (unsigned row = 0; row < 3; ++row) {
    float a = base[row * 4], b = base[row * 4 + 1];
    base[row * 4] = a * cz + b * sz;
    base[row * 4 + 1] = b * cz - a * sz;
  }
  float cy = base_tilt[direction(tilt)][0];
  float sy = base_tilt[direction(tilt)][1];
  for (unsigned row = 0; row < 3; ++row) {
    float a = base[row * 4], b = base[row * 4 + 2];
    base[row * 4] = a * cy - b * sy;
    base[row * 4 + 2] = a * sy + b * cy;
  }
  translate(1);
  return base;
}

static Matrix multiply(const Matrix &a, const Matrix &b) {
  Matrix result{};
  for (unsigned row = 0; row < 3; ++row) {
    for (unsigned column = 0; column < 3; ++column)
      for (unsigned k = 0; k < 3; ++k)
        result[row * 4 + column] += a[row * 4 + k] * b[k * 4 + column];
    result[row * 4 + 3] = a[row * 4 + 3];
    for (unsigned k = 0; k < 3; ++k)
      result[row * 4 + 3] += a[row * 4 + k] * b[k * 4 + 3];
  }
  return result;
}

SceneImage draw_scene(const std::vector<ScenePart> &parts,
                      const Lighting &lighting) {
  struct Job {
    const Section *section;
    Matrix matrix;
    const std::array<std::uint8_t, 256> *light;
    float depth;
  };
  std::vector<Job> jobs;
  std::array<float, 3> minimum{INFINITY, INFINITY, INFINITY};
  std::array<float, 3> maximum{-INFINITY, -INFINITY, -INFINITY};
  for (const auto &part : parts) {
    if (!part.model || !part.animation ||
        part.frame >= part.animation->frames ||
        std::uint64_t(part.animation->frames) * part.animation->names.size() !=
            part.animation->matrices.size())
      throw std::runtime_error("invalid scene part");
    auto first = jobs.size();
    for (const auto &section : part.model->sections) {
      auto it = std::find(part.animation->names.begin(),
                          part.animation->names.end(), section.name);
      if (it == part.animation->names.end())
        throw std::runtime_error("VXL section missing in HVA");
      auto matrix = multiply(
          part.matrix,
          part.animation
              ->matrices[std::size_t(part.frame) *
                             part.animation->names.size() +
                         std::size_t(it - part.animation->names.begin())]);
      float depth = INFINITY;
      for (unsigned i = 0; i < 8; ++i) {
        float x = (i & 1) ? section.max[0] : section.min[0];
        float y = (i & 2) ? section.max[1] : section.min[1];
        float z = (i & 4) ? section.max[2] : section.min[2];
        for (unsigned axis = 0; axis < 3; ++axis) {
          float value = matrix[axis * 4] * x + matrix[axis * 4 + 1] * y +
                        matrix[axis * 4 + 2] * z + matrix[axis * 4 + 3];
          if (axis == 1)
            value = -value;
          if (!std::isfinite(value) || std::abs(value) > 100000)
            throw std::runtime_error("invalid scene matrix");
          minimum[axis] = std::min(minimum[axis], value);
          maximum[axis] = std::max(maximum[axis], value);
          if (axis == 2)
            depth = std::min(depth, value);
        }
      }
      jobs.push_back({&section, matrix, &part.normal_light, depth});
    }
    std::stable_sort(
        jobs.begin() + first, jobs.end(),
        [](const Job &a, const Job &b) { return a.depth < b.depth; });
  }
  if (jobs.empty())
    throw std::runtime_error("empty scene");
  if (maximum[0] - minimum[0] > 254 || maximum[1] - minimum[1] > 254)
    throw std::runtime_error("scene exceeds voxel surface");
  std::array<float, 3> center{};
  for (unsigned axis = 0; axis < 3; ++axis)
    center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
  std::vector<std::uint8_t> indices(65536);
  for (const auto &job : jobs)
    indices = raster_section_impl(*job.section, job.matrix, &lighting,
                                  job.light, &center, &indices);
  SceneImage result{
      {256, 256, std::vector<ra2::gfx::Rgba>(65536, {0, 0, 0, 0})},
      int(std::trunc(center[0])) - 128,
      int(std::trunc(center[1])) - 128};
  for (std::size_t i = 0; i < indices.size(); ++i)
    if (indices[i])
      result.image.pixels[i] = lighting.palette[indices[i]];
  return result;
}
VehiclePose vehicle_pose_from_map(std::uint8_t facing) {
  VehiclePose result;
  result.hull_facing = result.turret_facing = std::uint16_t(facing) << 8;
  return result;
}
VehicleMatrices vehicle_matrices(const VehiclePose &vehicle) {
  Matrix hull = base_facing_transform(vehicle.hull_facing, 0);
  Matrix turret =
      base_facing_transform(vehicle.turret_facing, vehicle.turret_offset);
  Matrix base = vehicle.turret.model ? turret : Matrix{1, 0, 0, 0, 0, 1,
                                                       0, 0, 0, 0, 1, 0};
  auto rotation =
      vehicle.turret.model ? std::uint16_t(0x4000) : vehicle.turret_facing;
  Matrix barrel = base_barrel_transform(base, rotation, vehicle.barrel_tilt);
  return {hull, turret, barrel};
}
SceneImage draw_vehicle(const VehiclePose &vehicle, const Lighting &lighting) {
  if (!vehicle.hull.model || !vehicle.hull.animation)
    throw std::runtime_error("vehicle hull is missing");
  std::vector<ScenePart> parts;
  std::vector<Animation> scaled_animations;
  scaled_animations.reserve(3);
  auto append = [&](const VehiclePart &source, const Matrix &local) {
    if ((source.model == nullptr) != (source.animation == nullptr))
      throw std::runtime_error("incomplete vehicle part");
    if (!source.model)
      return;
    if (!source.animation->frames)
      throw std::runtime_error("empty vehicle animation");
    scaled_animations.push_back(*source.animation);
    scale_hva_translations(scaled_animations.back(), *source.model);
    Matrix view = multiply(base_game_camera(), local);
    if (source.model->sections.empty())
      throw std::runtime_error("empty vehicle model");
    auto light = vehicle.original_lighting
                     ? base_vehicle_light(
                           view, source.model->sections.front().normal_type)
                     : source.normal_light;
    parts.push_back({source.model, &scaled_animations.back(), view,
                     source.frame % source.animation->frames, light});
  };
  auto matrices = vehicle_matrices(vehicle);
  append(vehicle.hull, matrices.hull);
  // Base caller 0x43B14A chooses which side of the turret receives the barrel.
  const unsigned quadrant =
      (((unsigned(vehicle.turret_facing) >> 13) + 1) >> 1) & 3;
  if (quadrant == 0 || quadrant == 3) {
    append(vehicle.barrel, matrices.barrel);
    append(vehicle.turret, matrices.turret);
  } else {
    append(vehicle.turret, matrices.turret);
    append(vehicle.barrel, matrices.barrel);
  }
  // The original caller renders each vehicle part into its own surface before
  // compositing it at that part's screen offset.
  std::vector<SceneImage> layers;
  int min_x = 1000000, min_y = 1000000, max_x = -1000000, max_y = -1000000;
  for (const auto &part : parts) {
    layers.push_back(draw_scene({part}, lighting));
    const auto &layer = layers.back();
    for (unsigned y = 0; y < layer.image.height; ++y)
      for (unsigned x = 0; x < layer.image.width; ++x)
        if (layer.image.pixels[std::size_t(y) * layer.image.width + x].a) {
          min_x = std::min(min_x, layer.left + int(x));
          min_y = std::min(min_y, layer.top + int(y));
          max_x = std::max(max_x, layer.left + int(x));
          max_y = std::max(max_y, layer.top + int(y));
        }
  }
  if (max_x < min_x)
    return {{256, 256, std::vector<ra2::gfx::Rgba>(65536, {0, 0, 0, 0})}, 0, 0};
  if (max_x - min_x >= 256 || max_y - min_y >= 256)
    throw std::runtime_error("vehicle exceeds voxel surface");
  SceneImage result{
      {256, 256, std::vector<ra2::gfx::Rgba>(65536, {0, 0, 0, 0})},
      min_x,
      min_y};
  for (const auto &layer : layers)
    for (unsigned y = 0; y < layer.image.height; ++y)
      for (unsigned x = 0; x < layer.image.width; ++x) {
        const auto pixel =
            layer.image.pixels[std::size_t(y) * layer.image.width + x];
        if (!pixel.a)
          continue;
        auto dx = layer.left + int(x) - min_x, dy = layer.top + int(y) - min_y;
        result.image.pixels[std::size_t(dy) * 256 + std::size_t(dx)] = pixel;
      }
  return result;
}

Image raster_rgba(const Section &section, const std::array<float, 12> &matrix,
                  const Lighting &lighting,
                  const std::array<std::uint8_t, 256> &normal_light) {
  auto indices = raster_section(section, matrix, &lighting, &normal_light);
  Image image{256, 256,
              std::vector<ra2::gfx::Rgba>(indices.size(), {0, 0, 0, 0})};
  for (std::size_t i = 0; i < indices.size(); ++i)
    if (indices[i])
      image.pixels[i] = lighting.palette[indices[i]];
  return image;
}
Image draw(const Model &m, const Animation &a, const Lighting &l,
           unsigned frame, unsigned level, unsigned width, unsigned height,
           float angle, float zoom,
           const std::array<std::uint8_t, 256> *normal_light) {
  if (std::uint64_t(a.frames) * a.names.size() != a.matrices.size() ||
      frame >= a.frames || level >= l.tables.size() || !width || !height ||
      std::uint64_t(width) * height > 100000000 || !std::isfinite(angle) ||
      !std::isfinite(zoom) || zoom <= 0)
    throw std::runtime_error("invalid view");
  Image image{
      width, height,
      std::vector<ra2::gfx::Rgba>(std::size_t(width) * height, {0, 0, 0, 0})};
  std::vector<float> depth(image.pixels.size(),
                           -std::numeric_limits<float>::infinity());
  float c = std::cos(angle), s = std::sin(angle);
  for (const auto &sec : m.sections) {
    auto it = std::find(a.names.begin(), a.names.end(), sec.name);
    if (it == a.names.end())
      throw std::runtime_error("VXL section missing in HVA");
    if (!sec.x || !sec.y || !sec.z)
      throw std::runtime_error("invalid VXL dimensions");
    auto idx = std::size_t(it - a.names.begin());
    const auto &mat = a.matrices[std::size_t(frame) * a.names.size() + idx];
    for (auto v : sec.points) {
      float x = sec.min[0] + (sec.max[0] - sec.min[0]) * (v.x + 0.5f) / sec.x,
            y = sec.min[1] + (sec.max[1] - sec.min[1]) * (v.y + 0.5f) / sec.y,
            z = sec.min[2] + (sec.max[2] - sec.min[2]) * (v.z + 0.5f) / sec.z;
      float X = mat[0] * x + mat[1] * y + mat[2] * z + mat[3],
            Y = mat[4] * x + mat[5] * y + mat[6] * z + mat[7],
            Z = mat[8] * x + mat[9] * y + mat[10] * z + mat[11];
      float rx = c * X - s * Y, ry = s * X + c * Y;
      float px = width / 2.f + rx * zoom,
            py = height / 2.f + (ry * 0.5f - Z) * zoom;
      if (!std::isfinite(px) || !std::isfinite(py) || px < 0 || py < 0 ||
          px >= width || py >= height)
        continue;
      auto index = std::size_t(py) * width + std::size_t(px);
      float d = ry + Z * 0.5f;
      if (d > depth[index]) {
        depth[index] = d;
        unsigned pixel_level = normal_light ? (*normal_light)[v.normal] : level;
        if (pixel_level >= l.tables.size())
          throw std::runtime_error("light level outside VPL tables");
        image.pixels[index] = l.palette[l.tables[pixel_level][v.color]];
      }
    }
  }
  return image;
}
} // namespace ra2::voxel
