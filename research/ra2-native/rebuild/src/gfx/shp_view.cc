// shp_view - decode one frame of a named SHP from the game archives.
// Usage: shp_view <game-dir> <NAME.SHP> <frame> <out.ppm>
// Prints dimensions, frame count, and opaque-pixel stats.
#include "palette.h"
#include "shp_decode.h"
#include "mix_bootstrap.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>

using namespace ra2;
using namespace ra2::gfx;
using namespace ra2::fs;

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr,
                     "usage: shp_view <game-dir> <NAME.SHP> <frame> <out.ppm>\n");
        return 2;
    }
    const std::string name = argv[2];
    const std::size_t frame_index = static_cast<std::size_t>(std::atoi(argv[3]));
    const char* out_path = argv[4];

    MixFileSystem files(argv[1]);
    files.bootstrap();
    // Cameos and effect sprites live in the base archives; the full
    // resource mount needs CAMEOMD.MIX (on disc 2), which this game
    // dir does not carry. Base mounts are enough for these SHPs.

    for (const char* container : {"CONQUER.MIX", "ISOGEN.MIX", "ECACHE007.MIX"}) {
        try { files.mount(container, false); } catch (const MixError&) {}
    }
    auto bytes = files.read_file(name);
    if (!bytes) {
        std::fprintf(stderr, "%s not found in any mounted archive\n",
                     name.c_str());
        return 1;
    }
    auto pal_bytes = files.read_file("UNITTEM.PAL");
    if (!pal_bytes) {
        std::fprintf(stderr, "UNITTEM.PAL not found\n");
        return 1;
    }
    const Palette palette = Palette::from_bytes(*pal_bytes);
    const ShpImage shp = ShpImage::parse(*bytes);
    if (frame_index >= shp.frames().size()) {
        std::fprintf(stderr, "frame %zu out of range (%zu frames)\n",
                     frame_index, shp.frames().size());
        return 1;
    }
    const RgbaImage image = shp.decode_frame(frame_index, palette);
    // Count opaque pixels and distinct RGB colors in one pass.
    unsigned opaque = 0;
    std::set<unsigned> colors;
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        if (image.pixels[i + 3] == 0) continue;
        ++opaque;
        colors.insert((unsigned(image.pixels[i]) << 16) |
                      (unsigned(image.pixels[i + 1]) << 8) |
                      unsigned(image.pixels[i + 2]));
    }
    std::printf("%s frame %zu: %ux%u, %zu frames, opaque=%u colors=%zu\n",
                name.c_str(), frame_index, shp.header().width,
                shp.header().height, shp.frames().size(), opaque,
                colors.size());

    std::ofstream out(out_path, std::ios::binary);
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        out.put(static_cast<char>(image.pixels[i]));
        out.put(static_cast<char>(image.pixels[i + 1]));
        out.put(static_cast<char>(image.pixels[i + 2]));
    }
    return 0;
}
