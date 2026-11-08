#include "shp_decode.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace ra2::gfx {
namespace {
std::uint16_t le16(const std::vector<std::uint8_t>& bytes, std::size_t p) {
    return static_cast<std::uint16_t>(unsigned(bytes[p]) | (unsigned(bytes[p + 1]) << 8));
}
std::uint32_t le32(const std::vector<std::uint8_t>& bytes, std::size_t p) {
    return std::uint32_t(le16(bytes, p)) | (std::uint32_t(le16(bytes, p + 2)) << 16);
}
std::int16_t signed16(std::uint16_t value) {
    return static_cast<std::int16_t>(value <= 0x7fff ? int(value) : int(value) - 0x10000);
}
void check_pixels(std::uint16_t width, std::uint16_t height, std::size_t limit) {
    const auto count = std::uint64_t(width) * height;
    if (count > limit || count > std::numeric_limits<std::size_t>::max() / 4)
        throw GraphicsError("SHP pixel limit exceeded");
}
void require_range(std::size_t p, std::size_t count, std::size_t end) {
    if (p > end || count > end - p) throw GraphicsError("Truncated SHP frame data");
}
} // namespace

ShpImage ShpImage::parse(std::vector<std::uint8_t> bytes, std::size_t max_pixels) {
    if (bytes.size() < 8) throw GraphicsError("Truncated SHP header");
    if (le16(bytes, 0) != 0) throw GraphicsError("Unsupported SHP header marker (expected TS/RA2)");
    ShpImage shp;
    shp.header_ = {le16(bytes, 2), le16(bytes, 4), le16(bytes, 6)};
    check_pixels(shp.header_.width, shp.header_.height, max_pixels);
    const std::size_t table_end = 8 + std::size_t(shp.header_.frame_count) * 24;
    if (table_end > bytes.size()) throw GraphicsError("Truncated SHP frame table");
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < shp.header_.frame_count; ++i) {
        const auto p = 8 + i * 24;
        ShpFrame f;
        f.x = signed16(le16(bytes, p));
        f.y = signed16(le16(bytes, p + 2));
        f.width = le16(bytes, p + 4);
        f.height = le16(bytes, p + 6);
        f.flags = bytes[p + 8]; // Adjacent bytes can contain 0xCC padding.
        std::copy_n(bytes.begin() + p + 12, 3, f.radar_color.begin());
        f.data_offset = le32(bytes, p + 20);
        check_pixels(f.width, f.height, max_pixels);
        if (f.data_offset != 0) {
            if (f.data_offset < table_end || f.data_offset > bytes.size())
                throw GraphicsError("SHP frame offset outside payload");
            starts.push_back(f.data_offset);
        }
        shp.frames_.push_back(f);
    }
    std::sort(starts.begin(), starts.end());
    // No encoded size field has been established. Bound each payload by the
    // next distinct offset, permitting shared offsets and unordered records.
    for (const auto& f : shp.frames_) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), f.data_offset);
        shp.frame_ends_.push_back(next == starts.end() ? bytes.size() : *next);
    }
    shp.bytes_ = std::move(bytes);
    return shp;
}

RgbaImage ShpImage::decode_frame(std::size_t index, const Palette& palette) const {
    if (index >= frames_.size()) throw GraphicsError("SHP frame index out of range");
    const auto& f = frames_[index];
    RgbaImage result{header_.width, header_.height,
                     std::vector<std::uint8_t>(std::size_t(header_.width) * header_.height * 4, 0)};
    if (f.data_offset == 0 || f.width == 0 || f.height == 0) return result;
    auto put_pixel = [&](unsigned x, unsigned y, std::uint8_t color) {
        if (color == 0) return;
        const int dx = int(f.x) + int(x), dy = int(f.y) + int(y);
        if (dx < 0 || dy < 0 || dx >= int(result.width) || dy >= int(result.height)) return;
        const auto out = (std::size_t(dy) * result.width + std::size_t(dx)) * 4;
        const auto& c = palette[color];
        result.pixels[out] = c.r; result.pixels[out + 1] = c.g;
        result.pixels[out + 2] = c.b; result.pixels[out + 3] = 255;
    };
    std::size_t p = f.data_offset;
    const auto end = frame_ends_[index];
    if (!f.scanline_rle()) {
        require_range(p, std::size_t(f.width) * f.height, end);
        for (unsigned y = 0; y < f.height; ++y)
            for (unsigned x = 0; x < f.width; ++x) put_pixel(x, y, bytes_[p++]);
        return result;
    }
    for (unsigned y = 0; y < f.height; ++y) {
        require_range(p, 2, end);
        const auto row_size = le16(bytes_, p);
        if (row_size < 2) throw GraphicsError("SHP scanline length is less than two");
        require_range(p, row_size, end);
        const auto row_end = p + row_size;
        p += 2;
        unsigned x = 0;
        while (p < row_end) {
            const auto color = bytes_[p++];
            if (color == 0) {
                if (p == row_end) throw GraphicsError("SHP scanline ends in a missing skip count");
                const unsigned skip = bytes_[p++];
                if (skip == 0 || skip > unsigned(f.width) - x)
                    throw GraphicsError("SHP zero run is empty or crosses scanline");
                x += skip;
            } else {
                if (x == f.width) throw GraphicsError("SHP literal crosses scanline");
                put_pixel(x++, y, color);
            }
        }
        if (x != f.width) throw GraphicsError("SHP scanline does not cover frame width");
    }
    return result;
}
} // namespace ra2::gfx
