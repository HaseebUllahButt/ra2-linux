#include "../fs/mix_bootstrap.h"
#include "voxel.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ra2::voxel;
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--flat-depth-adjust") {
      int height;
      while (std::cin >> height)
        std::cout << base_flat_vehicle_depth_adjustment(height) << '\n';
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--depth-visible") {
      int pixel, row, terrain;
      while (std::cin >> pixel >> row >> terrain)
        std::cout << base_depth_visible(static_cast<std::uint8_t>(pixel), row,
                                        static_cast<std::uint16_t>(terrain))
                  << '\n';
      return 0;
    }
    if (argc == 2 && (std::string(argv[1]) == "--depth-rows" ||
                      std::string(argv[1]) == "--flat-depth-rows")) {
      int base, origin, destination, clip, height, adjustment;
      while (std::cin >> base >> origin >> destination >> clip >> height >> adjustment) {
        auto row = std::string(argv[1]) == "--flat-depth-rows"
            ? base_flat_vehicle_depth_row({base, origin, destination, clip,
                                           height, adjustment})
            : base_depth_row(base, origin, destination, clip, height, adjustment);
        for (int i = 0; i != 8; ++i) {
          std::cout << row.value << ' ' << row.phase << ' ';
          base_depth_next_row(row);
        }
        std::cout << '\n';
      }
      return 0;
    }
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
    if (argc == 3 && std::string(argv[1]) == "--asset") {
      auto asset = get(argv[2]);
      std::cout.write(reinterpret_cast<const char *>(asset.data()),
                      asset.size());
      return std::cout ? 0 : 1;
    }
    if (argc == 4 && std::string(argv[1]) == "--depth-case") {
      int direction = std::stoi(argv[3]);
      if (direction < 0 || direction > 7)
        throw std::runtime_error("invalid depth direction");
      auto model = read_vxl(get((std::string(argv[2]) + ".vxl").c_str()));
      auto lighting = read_vpl(get("voxels.vpl"));
      std::array<std::uint8_t, 256> levels{};
      for (unsigned i = 0; i < 256; ++i)
        levels[i] = i % 32;
      Matrix matrix = {1,
                       -1,
                       0,
                       0,
                       .5f,
                       .5f,
                       1,
                       0,
                       direction & 1 ? -.2f : .2f,
                       direction & 2 ? -.3f : .3f,
                       direction & 4 ? -1.f : 1.f,
                       0};
      auto raster = raster_section_depth(model.sections.at(0), matrix,
                                         &lighting, &levels);
      std::int32_t origin[] = {raster.left, raster.top};
      std::cout.write(reinterpret_cast<const char *>(origin), sizeof origin);
      std::cout.write(reinterpret_cast<const char *>(raster.pixels.data()),
                      raster.pixels.size());
      std::cout.write(reinterpret_cast<const char *>(raster.depths.data()),
                      raster.depths.size());
      return std::cout ? 0 : 1;
    }
    if (argc == 4 && (std::string(argv[1]) == "--shadow-case" ||
                      std::string(argv[1]) == "--shadow-blocked")) {
      int facing = std::stoi(argv[3]);
      if (facing < 0 || facing > 255)
        throw std::runtime_error("invalid map facing");
      auto model = read_vxl(get((std::string(argv[2]) + ".vxl").c_str()));
      ShadowOcclusion blocked;
      if (std::string(argv[1]) == "--shadow-blocked")
        blocked = [](int x, int y) { return (x + y) % 3 == 0; };
      auto shadow = base_shadow_section(
          model.sections.at(0),
          base_facing_transform(std::uint16_t(facing) << 8, 0), blocked, 1000,
          2000);
      std::int32_t origin[] = {shadow.left, shadow.top};
      std::cout.write(reinterpret_cast<const char *>(origin), sizeof origin);
      std::cout.write(reinterpret_cast<const char *>(shadow.pixels.data()),
                      shadow.pixels.size());
      return std::cout ? 0 : 1;
    }
    if (argc == 5 && (std::string(argv[1]) == "--vehicle-case" ||
                      std::string(argv[1]) == "--vehicle-parts-case")) {
      std::string name(argv[2]);
      int facing = std::stoi(argv[3]), offset = std::stoi(argv[4]);
      if (facing < 0 || facing > 255)
        throw std::runtime_error("invalid map facing");
      auto hull = read_vxl(get((name + ".vxl").c_str()));
      auto hull_hva = read_hva(get((name + ".hva").c_str()));
      auto turret = read_vxl(get((name + "tur.vxl").c_str()));
      auto turret_hva = read_hva(get((name + "tur.hva").c_str()));
      auto barrel = read_vxl(get((name + "barl.vxl").c_str()));
      auto barrel_hva = read_hva(get((name + "barl.hva").c_str()));
      auto lighting = read_vpl(get("voxels.vpl"));
      auto input = vehicle_pose_from_map(static_cast<std::uint8_t>(facing));
      input.turret_offset = offset;
      input.hull = {&hull, &hull_hva};
      input.turret = {&turret, &turret_hva};
      input.barrel = {&barrel, &barrel_hva};
      if (std::string(argv[1]) == "--vehicle-parts-case") {
        auto parts = draw_vehicle_parts(input, lighting);
        std::uint32_t count = static_cast<std::uint32_t>(parts.size());
        std::cout.write(reinterpret_cast<const char *>(&count), sizeof count);
        for (const auto &part : parts) {
          std::int32_t bounds[] = {part.left, part.top, part.source_x,
                                   part.source_y, part.source_width,
                                   part.source_height};
          std::cout.write(reinterpret_cast<const char *>(bounds), sizeof bounds);
          for (auto pixel : part.image.pixels) {
            char rgba[] = {char(pixel.r), char(pixel.g), char(pixel.b),
                           char(pixel.a)};
            std::cout.write(rgba, sizeof rgba);
          }
        }
        return std::cout ? 0 : 1;
      }
      auto output = draw_vehicle(input, lighting);
      std::int32_t origin[] = {output.left, output.top};
      std::cout.write(reinterpret_cast<const char *>(origin), sizeof origin);
      for (auto pixel : output.image.pixels) {
        char rgba[] = {char(pixel.r), char(pixel.g), char(pixel.b),
                       char(pixel.a)};
        std::cout.write(rgba, sizeof rgba);
      }
      return std::cout ? 0 : 1;
    }
    if (argc == 2 && (std::string(argv[1]) == "--vehicle" ||
                      std::string(argv[1]) == "--vehicle-hull" ||
                      std::string(argv[1]) == "--vehicle-zero")) {
      auto hull_model = read_vxl(get("htnk.vxl"));
      auto hull_animation = read_hva(get("htnk.hva"));
      auto turret_model = read_vxl(get("htnktur.vxl"));
      auto turret_animation = read_hva(get("htnktur.hva"));
      auto barrel_model = read_vxl(get("htnkbarl.vxl"));
      auto barrel_animation = read_hva(get("htnkbarl.hva"));
      auto light = read_vpl(get("voxels.vpl"));
      VehiclePose input;
      input.hull = {&hull_model, &hull_animation};
      if (std::string(argv[1]) != "--vehicle-hull") {
        input.turret = {&turret_model, &turret_animation};
        input.barrel = {&barrel_model, &barrel_animation};
      }
      input.hull_facing = input.turret_facing = 0x4000;
      input.turret_offset = std::string(argv[1]) == "--vehicle-zero" ? 0 : 16;
      auto output = draw_vehicle(input, light);
      std::int32_t origin[] = {output.left, output.top};
      std::cout.write(reinterpret_cast<const char *>(origin), sizeof origin);
      for (auto pixel : output.image.pixels) {
        char rgba[] = {char(pixel.r), char(pixel.g), char(pixel.b),
                       char(pixel.a)};
        std::cout.write(rgba, sizeof rgba);
      }
      return std::cout ? 0 : 1;
    }
    auto vb = get("htnk.vxl"), hb = get("htnk.hva"), pb = get("voxels.vpl");
    auto v = read_vxl(vb);
    auto p = read_vpl(pb);
    std::array<std::uint8_t, 256> test_light{};
    for (unsigned i = 0; i < 256; ++i)
      test_light[i] = i % 32;
    const std::array<std::uint64_t, 8> lit_hashes = {
        0x22033d5fb411b1b6ULL, 0xfacfd818113b9c3bULL, 0x15dd8d3e13071ae8ULL,
        0x38f42422ba0e9945ULL, 0x27dd6dc261980fd8ULL, 0x54f887d1ece4e457ULL,
        0xab31afb7bec384e6ULL, 0xdf061d334a8fa1fcULL};
    const std::array<std::uint64_t, 8> raster_hashes = {
        0xe94e713a7da26f85ULL, 0x31d5532f1ebb6683ULL, 0x67288c0a6e6dc2a7ULL,
        0x5a02e9ef37c11204ULL, 0x7accc6d5c7971c81ULL, 0x2b42f044298bacd5ULL,
        0xe49771ab954cfb16ULL, 0xf8681956dd883719ULL};
    for (unsigned direction = 0; direction < 8; ++direction) {
      std::array<float, 12> matrix = {1,
                                      -1,
                                      0,
                                      0,
                                      .5f,
                                      .5f,
                                      1,
                                      0,
                                      direction & 1 ? -.2f : .2f,
                                      direction & 2 ? -.3f : .3f,
                                      direction & 4 ? -1.f : 1.f,
                                      0};
      auto pixels = raster_section(v.sections[0], matrix);
      std::uint64_t hash = 14695981039346656037ULL;
      for (auto pixel : pixels)
        hash = (hash ^ pixel) * 1099511628211ULL;
      auto lit = raster_section(v.sections[0], matrix, &p, &test_light);
      std::uint64_t lit_hash = 14695981039346656037ULL;
      for (auto pixel : lit)
        lit_hash = (lit_hash ^ pixel) * 1099511628211ULL;
      if (lit_hash != lit_hashes[direction])
        throw std::runtime_error("x86 lit raster mismatch direction " +
                                 std::to_string(direction));
      if (hash != raster_hashes[direction])
        throw std::runtime_error("x86 raster mismatch direction " +
                                 std::to_string(direction) + " actual " +
                                 std::to_string(hash));
    }
    auto h = read_hva(hb);
    auto initial_pose = vehicle_pose_from_map(0x72);
    if (initial_pose.hull_facing != 0x7200 ||
        initial_pose.turret_facing != 0x7200 ||
        initial_pose.barrel_tilt != 0x4000 || initial_pose.turret_offset != 0)
      throw std::runtime_error("base map facing mismatch");
    const Matrix light_identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const std::array<std::uint64_t, 4> light_hashes = {
        0xa63457ac98d80794ULL, 0x64f4499b06f451ecULL, 0x44480f1599dcda35ULL,
        0xf4c3d01914adbea1ULL};
    unsigned light_case = 0;
    for (const auto &matrix : {light_identity, base_game_camera()})
      for (unsigned type : {2u, 4u}) {
        auto table = base_vehicle_light(matrix, type);
        std::uint64_t digest = 14695981039346656037ULL;
        for (auto value : table)
          digest = (digest ^ value) * 1099511628211ULL;
        if (digest != light_hashes[light_case++])
          throw std::runtime_error("base vehicle light mismatch");
      }
    auto base_camera = base_game_camera();
    if (base_camera[0] != 0.7081906199455261f ||
        base_camera[6] != 0.865897536277771f)
      throw std::runtime_error("base camera mismatch");
    auto base_face = base_facing_transform(0x8000, 17);
    if (base_face[0] != 0.0015339801320806146f ||
        base_face[7] != -1.999997615814209f)
      throw std::runtime_error("base facing mismatch");
    if (v.sections.empty() || v.sections[0].points.empty() || h.frames != 1 ||
        p.tables.size() != 32)
      throw std::runtime_error("asset mismatch");
    ScenePart hull{
        &v, &h, {1, -1, 0, 0, .5f, .5f, 1, 0, .2f, .3f, 1, 0}, 0, test_light};
    auto scene = draw_scene({hull}, p);
    if (scene.left != -125 || scene.top != -135)
      throw std::runtime_error("x86 scene origin mismatch");
    auto oracle = raster_rgba(v.sections[0], hull.matrix, p, test_light);
    if (scene.image.pixels.size() != oracle.pixels.size())
      throw std::runtime_error("scene size mismatch");
    for (std::size_t i = 0; i < oracle.pixels.size(); ++i)
      if (scene.image.pixels[i].r != oracle.pixels[i].r ||
          scene.image.pixels[i].g != oracle.pixels[i].g ||
          scene.image.pixels[i].b != oracle.pixels[i].b ||
          scene.image.pixels[i].a != oracle.pixels[i].a)
        throw std::runtime_error("scene section mismatch");
    auto face = facing_transform(0x4000, 17);
    if (std::abs(face[0] - 1) > 0.00001f || std::abs(face[3] - 2) > 0.00001f ||
        std::abs(face[7]) > 0.00001f)
      throw std::runtime_error("facing transform mismatch");
    auto north = facing_transform(0, 17);
    if (std::abs(north[0]) > 0.00001f || std::abs(north[4] - 1) > 0.00001f ||
        std::abs(north[3]) > 0.00001f || std::abs(north[7] - 2) > 0.00001f)
      throw std::runtime_error("facing quarter turn mismatch");
    Matrix base_barrel = {0, -1, 0, 0, 1, 0, 0, 2, 0, 0, 1, 0};
    auto barrel = barrel_transform(base_barrel, 0, 0);
    Matrix barrel_expected = {0, 0, 1, 2, 0, -1, 0, 0, 1, 0, 0, 0};
    for (unsigned i = 0; i < 12; ++i)
      if (std::abs(barrel[i] - barrel_expected[i]) > .00001f)
        throw std::runtime_error("barrel pivot mismatch");
    auto camera = game_camera();
    if (std::abs(camera[0] - 0.70710678f) > 0.00001f ||
        std::abs(camera[6] - 0.8660254f) > 0.00001f)
      throw std::runtime_error("game camera mismatch");
    auto image = draw(v, h, p, 0, 16, 128, 128, 0, 2);
    if (std::none_of(image.pixels.begin(), image.pixels.end(),
                     [](auto x) { return x.a != 0; }))
      throw std::runtime_error("empty image");
    for (const char *asset : {"htnktur", "mtnk", "mtnktur", "tnkd"}) {
      std::string base(asset);
      auto model = read_vxl(get((base + ".vxl").c_str()));
      auto animation = read_hva(get((base + ".hva").c_str()));
      if (base == "mtnktur") {
        ScenePart turret{&model, &animation, hull.matrix, 0, test_light};
        turret.matrix[3] += 4;
        auto combined = draw_scene({hull, turret}, p);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < combined.image.pixels.size(); ++i)
          changed += combined.image.pixels[i].r != scene.image.pixels[i].r ||
                     combined.image.pixels[i].g != scene.image.pixels[i].g ||
                     combined.image.pixels[i].b != scene.image.pixels[i].b ||
                     combined.image.pixels[i].a != scene.image.pixels[i].a;
        if (!changed)
          throw std::runtime_error("turret had no effect");
        for (unsigned direction : {0u, 7u}) {
          std::array<float, 12> matrix = {1,
                                          -1,
                                          0,
                                          0,
                                          .5f,
                                          .5f,
                                          1,
                                          0,
                                          direction & 1 ? -.2f : .2f,
                                          direction & 2 ? -.3f : .3f,
                                          direction & 4 ? -1.f : 1.f,
                                          0};
          auto pixels =
              raster_section(model.sections[0], matrix, &p, &test_light);
          std::uint64_t hash = 14695981039346656037ULL;
          for (auto pixel : pixels)
            hash = (hash ^ pixel) * 1099511628211ULL;
          if (hash !=
              (direction == 0 ? 0xc923cfb562c50019ULL : 0x058b2abb9bc4edf5ULL))
            throw std::runtime_error("x86 turret raster mismatch");
        }
      }

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
    std::array<float, 12> test_matrix = {1, -1, 0,   0,   .5f, .5f,
                                         1, 0,  .2f, .3f, 1,   0};
    auto indices =
        raster_section(v.sections[0], test_matrix, &team, &test_light);
    auto rgba = raster_rgba(v.sections[0], test_matrix, team, test_light);
    for (std::size_t i = 0; i < indices.size(); ++i) {
      if ((indices[i] == 0 && rgba.pixels[i].a != 0) ||
          (indices[i] != 0 && (rgba.pixels[i].a != 255 ||
                               rgba.pixels[i].r != team.palette[indices[i]].r)))
        throw std::runtime_error("raster RGBA palette mismatch");
    }
    if (team.palette[p.remap_first].r != 123 ||
        team.palette[1].r != p.palette[1].r || team.tables != p.tables)
      throw std::runtime_error("team range mismatch");
    auto dark = double_light(2, {0, 0, 0}, {0, 0, 0}, 1);
    auto opposite = double_light(2, {0.671214f, 0.198492f, -0.714194f},
                                 {-0.671214f, -0.198492f, 0.714194f}, 1);
    if (dark[0] != 0 || dark[253] != 16 || opposite[0] != 16)
      throw std::runtime_error("double light mismatch");
    auto turret_model = read_vxl(get("htnktur.vxl"));
    auto turret_animation = read_hva(get("htnktur.hva"));
    auto barrel_model = read_vxl(get("htnkbarl.vxl"));
    auto barrel_animation = read_hva(get("htnkbarl.hva"));
    VehiclePose vehicle;
    vehicle.hull = {&v, &h, 0, test_light};
    vehicle.turret = {&turret_model, &turret_animation, 0, test_light};
    vehicle.barrel = {&barrel_model, &barrel_animation, 0, test_light};
    vehicle.hull_facing = 0x4000;
    vehicle.turret_facing = 0x4000;
    vehicle.turret_offset = 16;
    auto first_vehicle = draw_vehicle(vehicle, p);
    vehicle.turret_facing = 0x8000;
    auto transforms = vehicle_matrices(vehicle);
    const Matrix expected_turret = {0.0015339801320806146f,
                                    0.9999988079071045f,
                                    0,
                                    0.003067960264161229f,
                                    -0.9999988079071045f,
                                    0.0015339801320806146f,
                                    0,
                                    -1.999997615814209f,
                                    0,
                                    0,
                                    1,
                                    0};
    const Matrix expected_barrel = {0.0015339801320806146f,
                                    0.9999988079071045f,
                                    0,
                                    0.003067731624469161f,
                                    -0.9999988079071045f,
                                    0.0015339801320806146f,
                                    0,
                                    -1.9999973773956299f,
                                    0,
                                    0,
                                    1,
                                    0};
    for (unsigned i = 0; i < 12; ++i)
      if (std::abs(transforms.turret[i] - expected_turret[i]) > 0.000001f ||
          std::abs(transforms.barrel[i] - expected_barrel[i]) > 0.000001f)
        throw std::runtime_error("base vehicle placement mismatch");
    vehicle.turret_facing = 0x2000;
    vehicle.barrel_tilt = 0;
    vehicle.turret_offset = -16;
    auto tilted = vehicle_matrices(vehicle);
    const Matrix expected_tilted = {
        0.0010846876539289951f, -0.7071067690849304f,
        -0.7071058750152588f,   -0.4157473146915436f,
        0.0010846876539289951f, 0.7071067690849304f,
        -0.7071058750152588f,   -0.41574758291244507f,
        0.9999988079071045f,    0,
        0.0015339801320806146f, -1.4142117500305176f};
    for (unsigned i = 0; i < 12; ++i)
      if (std::abs(tilted.barrel[i] - expected_tilted[i]) > 0.000002f)
        throw std::runtime_error("base tilted barrel mismatch");
    vehicle.turret_facing = 0x8000;
    vehicle.barrel_tilt = 0x4000;
    vehicle.turret_offset = 16;
    auto second_vehicle = draw_vehicle(vehicle, p);
    std::size_t vehicle_diff = 0;
    for (std::size_t i = 0; i < first_vehicle.image.pixels.size(); ++i) {
      auto a = first_vehicle.image.pixels[i],
           b = second_vehicle.image.pixels[i];
      vehicle_diff += a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a;
    }
    if (!vehicle_diff)
      throw std::runtime_error("turret facing did not change vehicle");
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
