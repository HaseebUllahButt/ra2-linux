#include "../fs/mix_bootstrap.h"
#include "voxel.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ra2::voxel;
int main() {
  try {
    ra2::fs::MixFileSystem fs("/path/to/user/ra2/game");
    fs.bootstrap();
    fs.mount("conquer.mix", false);
    fs.mount("conqmd.mix", false);
    auto get = [&](const char *n) {
      auto b = fs.read_file(n);
      if (!b)
        throw std::runtime_error(std::string("missing ") + n);
      return *b;
    };
    auto vb = get("htnk.vxl"), hb = get("htnk.hva"), pb = get("voxels.vpl");
    auto v = read_vxl(vb);
    auto h = read_hva(hb);
    auto p = read_vpl(pb);
    if (v.sections.empty() || v.sections[0].points.empty() || h.frames != 1 ||
        p.tables.size() != 32)
      throw std::runtime_error("asset mismatch");
    auto image = draw(v, h, p, 0, 16, 128, 128, 0, 2);
    if (std::none_of(image.pixels.begin(), image.pixels.end(),
                     [](auto x) { return x.a != 0; }))
      throw std::runtime_error("empty image");
    for (const char *asset : {"htnktur", "mtnk", "mtnktur", "tnkd"}) {
      std::string base(asset);
      auto model = read_vxl(get((base + ".vxl").c_str()));
      auto animation = read_hva(get((base + ".hva").c_str()));
      auto result = draw(model, animation, p, 0, 16, 128, 128, 0, 2);
      if (model.sections.empty() || model.sections[0].points.empty() ||
          std::none_of(result.pixels.begin(), result.pixels.end(),
                       [](auto pixel) { return pixel.a; }))
        throw std::runtime_error("vehicle image missing");
      std::cout << asset << " points=" << model.sections[0].points.size()
                << '\n';
    }
    Section projection_section{};
    projection_section.min = {-1, -2, 1};
    projection_section.max = {4, 3, 5};
    std::array<float, 12> section_matrix = {1, 0, 1, 2, 0, 1, 2, 3, 0, 0, 1, 4};
    std::array<float, 12> view_matrix = {2, 0, 0, 1, 0, 3, 0, -1, 0, 0, 1, 0};
    auto projected = project_ground_corners(projection_section, section_matrix,
                                            view_matrix, {5, 7, 0});
    const std::array<ProjectedCorner, 4> projected_expected = {
        {{20, -30}, {20, -15}, {10, -15}, {10, -30}}};
    if (projected != projected_expected)
      throw std::runtime_error("ground projection mismatch");
    Section coverage_section{};
    coverage_section.x = 2;
    coverage_section.y = 2;
    coverage_section.occupied_columns = {1, 0, 0, 1};
    auto coverage_case =
        [&](std::array<ProjectedCorner, 4> corners,
            std::initializer_list<std::pair<unsigned, unsigned>> expected) {
          auto coverage = ground_coverage(coverage_section, corners, {0, 0});
          if (std::count(coverage.begin(), coverage.end(), 1) !=
              static_cast<int>(expected.size()))
            throw std::runtime_error("coverage count mismatch");
          for (auto xy : expected)
            if (!coverage[xy.second * 256 + xy.first])
              throw std::runtime_error("coverage position mismatch");
        };
    coverage_case({{{0, 0}, {4, 0}, {0, 0}, {0, 4}}},
                  {{128, 128}, {129, 128}, {130, 130}, {131, 130}});
    coverage_case({{{0, 0}, {-1.5f, 1}, {3.5f, -2}, {5.5f, 5}}},
                  {{131, 126}, {132, 126}, {130, 131}, {131, 131}});
    coverage_case({{{0, 0}, {129, 0}, {127, 0}, {127, 2}}},
                  {{255, 128}, {0, 129}, {1, 129}});
    auto scaled = h;
    scaled.matrices[0][3] = 12;
    scaled.matrices[0][7] = -8;
    scaled.matrices[0][11] = 4;
    scaled.matrices[0][0] = 3;
    scale_hva_translations(scaled, v);
    if (scaled.matrices[0][3] != 12 * v.sections[0].scale ||
        scaled.matrices[0][7] != -8 * v.sections[0].scale ||
        scaled.matrices[0][11] != 4 * v.sections[0].scale ||
        scaled.matrices[0][0] != 3)
      throw std::runtime_error("HVA scaling mismatch");
    auto generated = make_default_lighting(p);
    unsigned equal = 0;
    for (unsigned row = 0; row < 32; ++row)
      for (unsigned col = 1; col < 256; ++col) {
        auto out = generated.tables[row][col];
        bool source_remap = col >= p.remap_first && col <= p.remap_last;
        bool dest_remap = out >= p.remap_first && out <= p.remap_last;
        if (source_remap != dest_remap)
          throw std::runtime_error("lighting range crossed");
        if (out != p.tables[row][col])
          std::cout << "VPL mismatch row=" << row << " color=" << col
                    << " got=" << unsigned(out)
                    << " expected=" << unsigned(p.tables[row][col]) << '\n';
        equal += out == p.tables[row][col];
      }
    if (equal < 8100)
      throw std::runtime_error("VPL shade table mismatch");
    std::cout << "matching VPL table bytes=" << equal << "/8160\n";
    auto colors = p.palette;
    colors[p.remap_first] = {123, 45, 67, 255};
    colors[1] = {99, 88, 77, 255};
    auto team = team_colors(p, colors);
    if (team.palette[p.remap_first].r != 123 ||
        team.palette[1].r != p.palette[1].r || team.tables != p.tables)
      throw std::runtime_error("team range mismatch");
    auto dark = double_light(2, {0, 0, 0}, {0, 0, 0}, 1);
    auto opposite = double_light(2, {0.671214f, 0.198492f, -0.714194f},
                                 {-0.671214f, -0.198492f, 0.714194f}, 1);
    if (dark[0] != 0 || dark[253] != 16 || opposite[0] != 16)
      throw std::runtime_error("double light mismatch");
    auto light = single_light(2, {0.671214f, 0.198492f, -0.714194f});
    if (light[0] != 16 || light[253] != 16 || light[254] != 16 ||
        light[255] != 16)
      throw std::runtime_error("single light lookup mismatch");
    (void)draw(v, h, p, 0, 16, 128, 128, 0, 2, &light);
    if (p.palette[1].r != pb[19] || p.palette[1].g != pb[20])
      throw std::runtime_error("VPL palette conversion mismatch");
    auto reject = [](auto operation) {
      try {
        operation();
      } catch (const std::runtime_error &) {
        return;
      }
      throw std::runtime_error("invalid input accepted");
    };
    reject([&] { (void)draw(v, h, p, 0, 16, 0, 128, 0, 2); });
    auto wrong = h;
    wrong.names[0] = "missing";
    reject([&] { (void)draw(v, wrong, p, 0, 16, 128, 128, 0, 2); });
    auto bad_vpl = pb;
    bad_vpl[0] = 255;
    reject([&] { (void)read_vpl(bad_vpl); });
    auto bad_model = vb;
    bad_model[bad_model.size() - 4] = 0;
    reject([&] { (void)read_vxl(bad_model); });
    for (auto *b : {&vb, &hb, &pb}) {
      auto copy = *b;
      copy.resize(8);
      bool rejected = false;
      try {
        if (b == &vb)
          (void)read_vxl(copy);
        else if (b == &hb)
          (void)read_hva(copy);
        else
          (void)read_vpl(copy);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      if (!rejected)
        throw std::runtime_error("accepted truncated file");
    }
    std::cout
        << "HTNK VXL/HVA, VOXELS VPL, drawing and truncation passed; points="
        << v.sections[0].points.size() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
