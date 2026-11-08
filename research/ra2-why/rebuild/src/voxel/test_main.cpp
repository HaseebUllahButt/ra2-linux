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
