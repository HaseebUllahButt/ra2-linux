#include "voxel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace ra2::voxel {
namespace {
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
    auto starts = u32(b, t), ends = u32(b, t + 4), data = u32(b, t + 8);
    if (starts > body_size || ends > body_size || data > body_size ||
        count * 4 > body_size - starts || count * 4 > body_size - ends)
      throw std::runtime_error("invalid VXL tables");
    for (std::size_t i = 0; i < count; ++i) {
      auto a = u32(b, body + starts + i * 4), z = u32(b, body + ends + i * 4);
      if (a == 0xffffffff && z == 0xffffffff)
        continue;
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
        std::clamp(std::nearbyint(value * 16), 0.0f, 31.0f));
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
    // 0x758670 clamps negative values, multiplies by 16 and rounds.
    result[i] = static_cast<std::uint8_t>(
        std::clamp(std::nearbyint(dot * 16), 0.0f, 31.0f));
  }
  result[253] = result[254] = result[255] = 16;
  return result;
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
