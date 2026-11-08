#include "shp_decode.h"
#include "mix_bootstrap.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

using namespace ra2::gfx;
using namespace ra2::fs;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action) {
    try { action(); } catch (const GraphicsError&) { return; }
    throw std::runtime_error("Malformed graphics input was accepted");
}
void put16(std::vector<std::uint8_t>& b, std::size_t p, unsigned v) {
    b.at(p) = static_cast<std::uint8_t>(v); b.at(p + 1) = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t p, unsigned v) {
    put16(b, p, v); put16(b, p + 2, v >> 16);
}
std::vector<std::uint8_t> fixture(std::uint8_t flags, std::vector<std::uint8_t> payload) {
    std::vector<std::uint8_t> b(32, 0);
    put16(b, 2, 5); put16(b, 4, 4); put16(b, 6, 1);
    put16(b, 8, 1); put16(b, 10, 1); put16(b, 12, 3); put16(b, 14, 2);
    b[16] = flags; b[17] = 0xcc; b[18] = 0xcc; b[19] = 0xcc;
    b[20] = 7; b[21] = 8; b[22] = 9;
    put32(b, 28, 32);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}
void self_tests() {
    std::vector<std::uint8_t> pal_bytes(768, 0);
    pal_bytes[3] = 63; pal_bytes[7] = 32; pal_bytes[11] = 16;
    pal_bytes[12] = 255; // Byte shift behavior, not clamping or replication.
    const auto pal = Palette::from_bytes(pal_bytes);
    require(pal[1].r == 252 && pal[2].g == 128 && pal[3].b == 64 && pal[4].r == 252,
            "PAL channel expansion");
    for (auto size : {0u, 767u, 769u})
        rejects([&] { Palette::from_bytes(std::vector<std::uint8_t>(size)); });
    rejects([] { Palette::from_bytes(nullptr, 768); });
    const std::vector<std::uint8_t> raw{1, 0, 2, 0, 0, 3};
    // Two independent hand-encoded scanlines, including each length word.
    const std::vector<std::uint8_t> rle{6, 0, 1, 0, 1, 2, 5, 0, 0, 2, 3};
    std::vector<std::uint8_t> expected(5 * 4 * 4, 0);
    auto expected_pixel = [&](std::size_t pixel, Rgba c) {
        expected[pixel * 4] = c.r; expected[pixel * 4 + 1] = c.g;
        expected[pixel * 4 + 2] = c.b; expected[pixel * 4 + 3] = 255;
    };
    expected_pixel(6, {252, 0, 0, 255});
    expected_pixel(8, {0, 128, 0, 255});
    expected_pixel(13, {0, 0, 64, 255});
    for (std::uint8_t flag : {0, 1, 2, 3}) {
        auto bytes = fixture(flag, (flag & 2) ? rle : raw);
        const auto shp = ShpImage::parse(bytes);
        require(shp.header().frame_count == 1 && shp.frames()[0].radar_color[2] == 9,
                "SHP header/metadata");
        require(shp.decode_frame(0, pal).pixels == expected, "Exact raw/RLE RGBA, offsets, transparency");
        rejects([&] { shp.decode_frame(1, pal); });
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            rejects([&] {
                ShpImage::parse(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n))
                    .decode_frame(0, pal);
            });
        }
    }
    // The real blitter stops at remaining width <= 0. A final transparent
    // run may exceed width, and the row's remaining bytes may be padding.
    auto extended = fixture(3, {6, 0, 1, 0, 1, 2, 5, 0, 0, 255, 99});
    auto extended_image = ShpImage::parse(extended).decode_frame(0, pal);
    auto extended_expected = expected;
    std::fill_n(extended_expected.begin() + 13 * 4, 4, 0);
    require(extended_image.pixels == extended_expected, "Terminal zero overshoot and row padding");
    auto noop = fixture(3, {8, 0, 0, 0, 1, 0, 1, 2, 5, 0, 0, 2, 3});
    require(ShpImage::parse(noop).decode_frame(0, pal).pixels == expected, "Zero-length skip consumes input");
    auto bytes = fixture(3, rle);
    put16(bytes, 8, 65535); put16(bytes, 10, 65535);
    auto clipped = ShpImage::parse(bytes).decode_frame(0, pal);
    require(clipped.pixels[4 + 2] == 64 && clipped.pixels[4 + 3] == 255 &&
            std::count(clipped.pixels.begin(), clipped.pixels.end(), 255) == 1,
            "Signed crop clipping");
    // NULL.SHP style: empty nominal canvas, two empty records, 0xCC flags.
    bytes.assign(56, 0); put16(bytes, 6, 2); bytes[16] = bytes[40] = 0xcc;
    require(ShpImage::parse(bytes).decode_frame(1, pal).pixels.empty(), "Empty SHP");
    bytes = fixture(0, raw); put32(bytes, 28, 0);
    auto blank = ShpImage::parse(bytes).decode_frame(0, pal);
    require(std::all_of(blank.pixels.begin(), blank.pixels.end(), [](auto v) { return v == 0; }),
            "Zero payload offset means absent frame");
    // Shared payload offsets must not make a zero-length inferred payload.
    bytes = fixture(3, rle);
    auto record = std::vector<std::uint8_t>(bytes.begin() + 8, bytes.begin() + 32);
    bytes.insert(bytes.begin() + 32, record.begin(), record.end());
    put16(bytes, 6, 2); put32(bytes, 28, 56); put32(bytes, 52, 56);
    auto shared = ShpImage::parse(bytes);
    require(shared.decode_frame(0, pal).pixels == shared.decode_frame(1, pal).pixels,
            "Shared frame offsets");
    // Frames can appear in a different order than their payloads.
    bytes.insert(bytes.end(), rle.begin(), rle.end());
    put32(bytes, 28, 67); put32(bytes, 52, 56);
    require(ShpImage::parse(bytes).decode_frame(0, pal).pixels == expected, "Unordered frame offsets");
    // A row cannot consume another frame's bytes even if still inside the file.
    put16(bytes, 56, 12);
    rejects([&] { ShpImage::parse(bytes).decode_frame(1, pal); });
    for (unsigned offset : {1u, 31u, 9999u}) {
        bytes = fixture(0, raw); put32(bytes, 28, offset);
        rejects([&] { ShpImage::parse(bytes); });
    }
    bytes = fixture(0, raw); bytes[0] = 255;
    rejects([&] { ShpImage::parse(bytes); });
    bytes = fixture(0, raw);
    rejects([&] { ShpImage::parse(bytes, 19); });
    put16(bytes, 12, 65535); put16(bytes, 14, 65535);
    rejects([&] { ShpImage::parse(bytes); });
    const std::vector<std::vector<std::uint8_t>> bad_rows{
        {0, 0}, {1, 0}, {255, 255}, // Invalid length words.
        {3, 0, 0},                 // Dangling zero token.
        {4, 0, 0, 0},              // Zero-length skip alone cannot cover a row.
        {3, 0, 1}                  // Incomplete row.
    };
    for (const auto& row : bad_rows)
        rejects([&] { ShpImage::parse(fixture(3, row)).decode_frame(0, pal); });
    std::cout << "Synthetic tests passed: exact RGBA for flags 0/1/2/3, clipping, empty/shared/"
                 "unordered frames, every-prefix truncation, malformed rows, offsets and limits.\n";
}
struct PixelStats {
    std::size_t opaque = 0, colors = 0;
    double variance = 0;
};
PixelStats pixel_stats(const RgbaImage& image) {
    PixelStats stats;
    std::set<std::uint32_t> colors;
    double sum[3]{}, squares[3]{};
    for (std::size_t p = 0; p < image.pixels.size(); p += 4) {
        if (!image.pixels[p + 3]) continue;
        ++stats.opaque;
        colors.insert((std::uint32_t(image.pixels[p]) << 16) |
                      (std::uint32_t(image.pixels[p + 1]) << 8) | image.pixels[p + 2]);
        for (unsigned c = 0; c < 3; ++c) {
            const double value = image.pixels[p + c];
            sum[c] += value; squares[c] += value * value;
        }
    }
    stats.colors = colors.size();
    if (stats.opaque) for (unsigned c = 0; c < 3; ++c)
        stats.variance += squares[c] / stats.opaque - std::pow(sum[c] / stats.opaque, 2);
    return stats;
}
void write_ppm(const RgbaImage& image) {
    // Keep generated evidence in the ignored build directory for this checkout.
    std::filesystem::create_directories("rebuild/build/gfx");
    const auto directory = std::filesystem::canonical("rebuild/build/gfx");
    const auto path = directory / "decoded_frame.ppm";
    require(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)),
            "Refusing a symlink output file");
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot create PPM");
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    // Transparent pixels already contain black RGB; PPM has no alpha channel.
    for (std::size_t p = 0; p < image.pixels.size(); p += 4)
        out.write(reinterpret_cast<const char*>(image.pixels.data() + p), 3);
    out.close();
    require(bool(out), "PPM write failed");
    std::cout << "Wrote " << path << '\n';
}
void real_archive_test() {
    MixFileSystem files("game");
    files.bootstrap();
    const std::filesystem::path loose = "game/conquer.mix";
    auto conquer = std::filesystem::exists(loose) ? MixArchive::open(loose) : files.mount("CONQUER.MIX");
    if (!std::filesystem::exists(loose))
        std::cout << "No loose conquer.mix; opened CONQUER.MIX through the existing MIX mounts.\n";
    std::cout << conquer->name() << ": " << conquer->entries().size() << " entries, backing offset "
              << conquer->backing_offset() << '\n';
    auto pal_bytes = files.read_file("UNITTEM.PAL");
    require(pal_bytes.has_value(), "UNITTEM.PAL not found in real MIX files");
    const auto palette = Palette::from_bytes(*pal_bytes);
    std::cout << "Loaded UNITTEM.PAL: " << pal_bytes->size() << " bytes, RGB channels expanded << 2.\n";
    // A MIX stores full-name hashes, not extensions or filenames. Guess whole
    // names, including .SHP, and let MixArchive perform the CRC lookup.
    std::set<std::string> candidates{"HVES.SHP", "HVEA.SHP", "OREGATH.SHP", "CHRONOSK.SHP",
                                     "PIPS.SHP", "FOG.SHP", "SHROUD.SHP"};
    std::ifstream names("scripts/binary_names.txt");
    require(bool(names), "Cannot read scripts/binary_names.txt");
    std::string name;
    while (std::getline(names, name)) {
        if (!name.empty() && name.back() == '\r') name.pop_back();
        for (char& c : name) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".SHP" &&
            name.find_first_of("*%?") == std::string::npos) candidates.insert(name);
    }
    std::size_t matched = 0, decoded = 0, raw = 0, rle = 0;
    std::map<unsigned, std::size_t> flag_counts;
    std::optional<RgbaImage> preview;
    std::string preview_name;
    for (const auto& candidate : candidates) {
        const auto* entry = conquer->find(candidate);
        if (!entry) continue;
        ++matched;
        const auto shp = ShpImage::parse(conquer->read(*entry));
        for (std::size_t i = 0; i < shp.frames().size(); ++i) {
            const auto& frame = shp.frames()[i];
            ++flag_counts[frame.flags];
            RgbaImage image;
            try { image = shp.decode_frame(i, palette); }
            catch (const GraphicsError& e) {
                throw GraphicsError(candidate + " frame " + std::to_string(i) + ": " + e.what());
            }
            ++decoded;
            if (frame.data_offset && frame.width && frame.height) {
                if (frame.scanline_rle()) ++rle; else ++raw;
            }
            if (i == 0 && (!preview || candidate == "OREGATH.SHP")) {
                const auto stats = pixel_stats(image);
                if (stats.opaque > 0 && stats.colors > 1 && stats.variance > 0) {
                    preview = image; preview_name = candidate;
                }
            }
        }
        std::cout << candidate << ": " << shp.header().width << 'x' << shp.header().height
                  << ", " << shp.frames().size() << " frames decoded\n";
    }
    require(matched > 0 && raw > 0 && rle > 0 && preview.has_value(),
            "Real archive must exercise raw and RLE frames and provide a varied preview");
    const auto stats = pixel_stats(*preview);
    require(stats.opaque > 0 && stats.colors > 1 && stats.variance > 0,
            "Decoded preview has no opaque RGB variance");
    std::cout << "Verified " << matched << " SHPs / " << decoded << " frames (" << raw << " raw, "
              << rle << " RLE, " << decoded - raw - rle << " absent/empty). Flag byte counts:";
    for (const auto& count : flag_counts) std::cout << ' ' << count.first << '=' << count.second;
    std::cout << "\nPreview " << preview_name << " frame 0: " << stats.opaque << " opaque pixels, "
              << stats.colors << " RGB colors, summed channel variance " << stats.variance << '\n';
    write_ppm(*preview);
}
} // namespace
int main(int argc, char** argv) {
    try {
        self_tests();
        if (argc == 2 && std::string(argv[1]) == "--self-test") return 0;
        require(argc == 1, "Usage: gfx_test [--self-test]");
        real_archive_test();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Graphics test failed: " << error.what() << '\n';
        return 1;
    }
}
