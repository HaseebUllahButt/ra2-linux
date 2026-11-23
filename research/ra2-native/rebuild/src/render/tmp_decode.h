#pragma once

#include "../gfx/palette.h"
#include <array>
#include <optional>

namespace ra2::render {
using Bytes = std::vector<std::uint8_t>;
using RenderError = gfx::GraphicsError;

struct Bitmap {
    int width = 0, height = 0;
    Bytes rgba; // Straight RGBA, row major.
};
struct TileLayer {
    int x = 0, y = 0; // Relative to this cell's diamond origin, not TMP origin.
    Bitmap bitmap;
    Bytes z; // Optional per-pixel depth bytes; same canvas as bitmap.
};
struct DecodedTile {
    TileLayer diamond;
    std::optional<TileLayer> extra;
};
struct TmpHeader {
    std::uint32_t columns = 0, rows = 0, tile_width = 0, tile_height = 0;
};
struct TmpRecord {
    bool present = false;
    std::int32_t x = 0, y = 0, extra_x = 0, extra_y = 0;
    std::uint32_t extra_width = 0, extra_height = 0, flags = 0;
    std::uint8_t height = 0, terrain = 0, ramp = 0;
    std::array<std::uint8_t, 3> radar_left{}, radar_right{};
};

// RA2 TMP, as consumed by 0x00545150 / 0x00547CF0. Owns its source bytes.
// The original renderer uses 60x30 tiles; other tile geometries are rejected.
class TmpImage {
public:
    static TmpImage parse(Bytes bytes);
    const TmpHeader& header() const noexcept { return header_; }
    const std::vector<TmpRecord>& records() const noexcept { return records_; }
    DecodedTile decode(std::size_t subtile, const gfx::Palette& palette) const;
private:
    struct Location { std::size_t start = 0, end = 0; };
    TmpHeader header_;
    std::vector<TmpRecord> records_;
    std::vector<Location> locations_;
    Bytes bytes_;
};
} // namespace ra2::render
