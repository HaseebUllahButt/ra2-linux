#include <fstream>
#include "minimap.h"
#include "../map/map_file.h"

#include <functional>
#include <iostream>
#include <set>

using namespace ra2::render;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw RenderError(message);
}
void put32(Bytes& b, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) b.at(p+i) = std::uint8_t(value >> (8*i));
}
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch (const RenderError&) { return; }
    throw RenderError("Malformed TMP was accepted");
}
void self_tests() {
    Bytes pal(768); pal[3] = 63; pal[7] = 63;
    const auto palette = ra2::gfx::Palette::from_bytes(pal);
    // One image and one absent subtile. Two raw diamond planes, followed by a
    // 2x2 overhang and its Z plane. Nonzero record origin checks relative extras.
    constexpr std::size_t start = 24;
    Bytes b(start+52+1800+8);
    put32(b, 0, 2); put32(b, 4, 1); put32(b, 8, 60); put32(b, 12, 30);
    put32(b, 16, start);
    put32(b, start, 30); put32(b, start+4, 15);
    put32(b, start+8, 1852); put32(b, start+12, 952); put32(b, start+16, 1856);
    put32(b, start+20, 29); put32(b, start+24, 13);
    put32(b, start+28, 2); put32(b, start+32, 2); put32(b, start+36, 3);
    std::fill_n(b.begin()+start+52, 900, 1);
    b[start+52] = 0;
    std::fill_n(b.begin()+start+952, 900, 7);
    b[start+1852] = 2; b[start+1853] = 0; b[start+1854] = 1; b[start+1855] = 2;
    std::fill_n(b.begin()+start+1856, 4, 9);
    const auto tmp = TmpImage::parse(b);
    const auto decoded = tmp.decode(0, palette);
    std::size_t opaque = 0;
    for (std::size_t p = 3; p < decoded.diamond.bitmap.rgba.size(); p += 4)
        opaque += decoded.diamond.bitmap.rgba[p] != 0;
    require(opaque == 900, "Diamond must have 900 opaque pixels, including index zero");
    require(decoded.diamond.bitmap.rgba[28*4+3] == 255 &&
            decoded.diamond.bitmap.rgba[28*4] == 0 &&
            decoded.diamond.bitmap.rgba[(14*60)*4] == 252 &&
            decoded.diamond.bitmap.rgba[(29*60)*4+3] == 0,
            "Diamond layout or palette expansion");
    require(decoded.diamond.z[28] == 7 && decoded.extra && decoded.extra->x == -1 &&
            decoded.extra->y == -2 && decoded.extra->bitmap.rgba[1] == 252 &&
            decoded.extra->bitmap.rgba[7] == 0 && decoded.extra->z[0] == 9,
            "Extra pixels, transparency, relative origin or Z plane");
    rejects([&] { tmp.decode(1, palette); });
    rejects([&] { tmp.decode(2, palette); });
    for (auto size : {std::size_t(0), std::size_t(15), start+51, start+951, b.size()-1}) {
        auto truncated = b; truncated.resize(size);
        rejects([&] { TmpImage::parse(truncated); });
    }
    for (auto [field, value] : std::vector<std::pair<std::size_t, std::uint32_t>>{
             {0, 0xffffffff}, {8, 48}, {16, 4}, {start+8, 0xffffffff},
             {start+12, 0}, {start+16, 0xffffffff}, {start+28, 0xffffffff}}) {
        auto bad = b; put32(bad, field, value);
        rejects([&] { TmpImage::parse(bad); });
    }
    // Flags are bit masks. Inactive extra metadata can be uninitialized in real
    // files; it must not be dereferenced or treated as a dimension.
    auto no_extra = b;
    put32(no_extra, start+36, 0xcdcdcdca);
    put32(no_extra, start+8, 0xcdcdcdcd); put32(no_extra, start+28, 0xcdcdcdcd);
    require(!TmpImage::parse(no_extra).decode(0, palette).extra, "Inactive extra metadata");
    std::cout << "TMP checks passed: diamond layout, palette, extras, depth, sparse grid and malformed bounds.\n";
}
struct Stats { std::size_t colors, opaque; double variance; };
Stats stats(const Bitmap& b) {
    std::set<std::uint32_t> colors;
    std::size_t opaque = 0;
    double sum[3]{}, sum2[3]{};
    for (std::size_t p = 0; p < b.rgba.size(); p += 4) {
        if (!b.rgba[p+3]) continue;
        ++opaque;
        colors.insert((std::uint32_t(b.rgba[p])<<16) | (std::uint32_t(b.rgba[p+1])<<8) | b.rgba[p+2]);
        for (unsigned c = 0; c < 3; ++c) { sum[c] += b.rgba[p+c]; sum2[c] += double(b.rgba[p+c])*b.rgba[p+c]; }
    }
    double variance = 0;
    if (opaque) for (unsigned c = 0; c < 3; ++c)
        variance += sum2[c]/opaque-(sum[c]/opaque)*(sum[c]/opaque);
    return {colors.size(), opaque, variance};
}
}

int main(int argc, char** argv) {
    try {
        self_tests();
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") return 0;
        require(argc == 1 || argc == 3, "Usage (from repo root): render_test [game-dir map-dir] | --self-test");
        const std::filesystem::path game = argc == 3 ? argv[1] : "game";
        const std::filesystem::path maps = argc == 3 ? argv[2] : "analysis/extracted/maps";
        const std::filesystem::path output = "rebuild/build/render";
        std::filesystem::create_directories(output);
        struct Expected { const char* name; int width, height; };
        for (const auto expected : {Expected{"2PEAKS", 4560, 2700}, {"DOWNTOWN", 4560, 2460}, {"FRSTBITE", 5700, 2610}}) {
            const auto* name = expected.name;
            const auto ini = ra2::rules::IniFile::from_file((maps/(std::string(name)+".MAP")).string());
            const auto geometry = MapGeometry::from_ini(ini);
            const auto terrain = ra2::pack::load_terrain(ini);
            ra2::fs::MixFileSystem files(game); files.bootstrap();
            const auto theater = ini.read_string("Map", "Theater");
            auto art = TheaterArt::load(files, theater);
            std::ifstream map_stream(maps / (std::string(name) + ".MAP"));
            const std::string map_text((std::istreambuf_iterator<char>(map_stream)),
                                       std::istreambuf_iterator<char>());
            ra2map::MapFile map_data;
            map_data.parse(map_text);
            const std::size_t placed = map_data.structures().size() +
                map_data.units().size() + map_data.infantry().size() +
                map_data.aircraft().size();
            require(placed > 0, std::string(name) + ": no placed objects");
            const auto image = render_minimap(terrain, geometry, art, &map_data);
            const auto s = stats(image);
            require(image.width == geometry.local.width*60 && image.height == geometry.local.height*30,
                    "Wrong native LocalSize dimensions");
            require(image.width == expected.width && image.height == expected.height,
                    std::string(name)+": dimensions differ from original map LocalSize");
            require(s.colors > 50 && s.variance > 100 && s.opaque > std::size_t(image.width)*image.height*98/100,
                    std::string(name)+": blank, flat, or mostly uncovered battlefield");
            const auto path = output/(std::string(name)+".ppm");
            write_ppm(image, path);
            require(std::filesystem::file_size(path) > std::size_t(image.width)*image.height*3,
                    "PPM output truncated");
            std::cout << name << " " << theater << ": " << terrain.tiles.size() << " records, "
                      << art.loaded_containers() << " TMPs, " << art.decoded_subtiles() << " subtiles; "
                      << art.palette_name() << " from " << art.palette_archive() << "; "
                      << image.width << 'x' << image.height << ", " << s.colors << " colors, "
                      << s.opaque << '/' << std::size_t(image.width)*image.height << " opaque, variance "
                      << s.variance << "; wrote " << path << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Render test failed: " << e.what() << '\n'; return 1;
    }
}
