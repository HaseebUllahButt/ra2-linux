#include "tmp_decode.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ra2::render {
namespace {
std::uint32_t u32(const Bytes& b, std::size_t p) {
    if (p > b.size() || b.size() - p < 4) throw RenderError("Truncated TMP word");
    return std::uint32_t(b[p]) | (std::uint32_t(b[p+1]) << 8) |
           (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
}
std::int32_t i32(const Bytes& b, std::size_t p) {
    auto word = u32(b, p);
    std::int32_t result;
    std::memcpy(&result, &word, sizeof(result));
    return result;
}
void range(std::size_t offset, std::size_t size, std::size_t end) {
    if (offset > end || size > end - offset) throw RenderError("TMP payload outside subtile");
}
void pixel(Bitmap& bitmap, std::size_t p, const gfx::Rgba& color) {
    bitmap.rgba[p*4] = color.r; bitmap.rgba[p*4+1] = color.g;
    bitmap.rgba[p*4+2] = color.b; bitmap.rgba[p*4+3] = 255;
}
constexpr std::size_t RecordSize = 52, DiamondSize = 900, MaxPixels = 16*1024*1024;
}

TmpImage TmpImage::parse(Bytes bytes) {
    TmpImage image;
    auto& h = image.header_;
    h = {u32(bytes, 0), u32(bytes, 4), u32(bytes, 8), u32(bytes, 12)};
    if (!h.columns || !h.rows || std::uint64_t(h.columns)*h.rows > 4096)
        throw RenderError("Invalid TMP subtile grid");
    if (h.tile_width != 60 || h.tile_height != 30)
        throw RenderError("Unsupported TMP tile geometry (expected RA2 60x30)");
    const std::size_t count = std::size_t(h.columns)*h.rows, table_end = 16+count*4;
    range(16, count*4, bytes.size());
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < count; ++i) {
        auto start = u32(bytes, 16+i*4);
        if (start) {
            if (start < table_end) throw RenderError("TMP subtile overlaps offset table");
            range(start, RecordSize+DiamondSize, bytes.size());
            offsets.push_back(start);
        }
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    image.records_.resize(count); image.locations_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t start = u32(bytes, 16+i*4);
        if (!start) continue;
        const auto next = std::upper_bound(offsets.begin(), offsets.end(), start);
        const auto end = next == offsets.end() ? bytes.size() : *next;
        range(start, RecordSize+DiamondSize, end);
        auto& r = image.records_[i];
        r.present = true; r.x = i32(bytes, start); r.y = i32(bytes, start+4);
        r.extra_x = i32(bytes, start+20); r.extra_y = i32(bytes, start+24);
        r.extra_width = u32(bytes, start+28); r.extra_height = u32(bytes, start+32);
        r.flags = u32(bytes, start+36);
        r.height = bytes[start+40]; r.terrain = bytes[start+41]; r.ramp = bytes[start+42];
        std::copy_n(bytes.begin()+start+43, 3, r.radar_left.begin());
        std::copy_n(bytes.begin()+start+46, 3, r.radar_right.begin());
        auto payload = [&](std::size_t field, std::size_t size) {
            const auto offset = u32(bytes, start+field);
            if (offset < RecordSize+DiamondSize) throw RenderError("TMP plane overlaps diamond/header");
            range(offset, size, end-start);
        };
        if (r.flags & 2) payload(12, DiamondSize);
        if (r.flags & 1) {
            const std::uint64_t pixels = std::uint64_t(r.extra_width)*r.extra_height;
            if (!pixels || pixels > MaxPixels) throw RenderError("Invalid TMP extra dimensions");
            const std::int64_t dx = std::int64_t(r.extra_x)-r.x, dy = std::int64_t(r.extra_y)-r.y;
            if (dx < -16384 || dx > 16384 || dy < -16384 || dy > 16384)
                throw RenderError("TMP extra origin exceeds render limit");
            payload(8, pixels);
            if (r.flags & 2) payload(16, pixels);
        }
        image.locations_[i] = {start, end};
    }
    image.bytes_ = std::move(bytes);
    return image;
}

DecodedTile TmpImage::decode(std::size_t subtile, const gfx::Palette& palette) const {
    if (subtile >= records_.size()) throw RenderError("TMP subtile index outside grid");
    const auto& r = records_[subtile];
    if (!r.present) throw RenderError("TMP subtile has no image");
    const auto start = locations_[subtile].start;
    DecodedTile result;
    auto& base = result.diamond;
    base.bitmap = {60, 30, Bytes(60*30*4)};
    if (r.flags & 2) base.z.resize(60*30);
    const auto z_start = (r.flags & 2) ? start+u32(bytes_, start+12) : 0;
    std::size_t source = 0;
    // 0x00547CF0: 29 scanlines, widths 4,8,...,60,...,8,4.
    for (int y = 0; y < 29; ++y) {
        const int width = 4*(y < 15 ? y+1 : 29-y), left = (60-width)/2;
        for (int x = left; x < left+width; ++x, ++source) {
            const auto p = std::size_t(y*60+x);
            // Unlike the rectangular extra plane, diamond index zero is opaque.
            pixel(base.bitmap, p, palette[bytes_[start+RecordSize+source]]);
            if (!base.z.empty()) base.z[p] = bytes_[z_start+source];
        }
    }
    if (r.flags & 1) {
        TileLayer extra;
        extra.x = int(std::int64_t(r.extra_x)-r.x); extra.y = int(std::int64_t(r.extra_y)-r.y);
        const std::size_t pixels = std::size_t(r.extra_width)*r.extra_height;
        extra.bitmap = {int(r.extra_width), int(r.extra_height), Bytes(pixels*4)};
        const auto source_start = start+u32(bytes_, start+8);
        for (std::size_t p = 0; p < pixels; ++p) {
            const auto index = bytes_[source_start+p];
            if (index) pixel(extra.bitmap, p, palette[index]);
        }
        if (r.flags & 2) {
            const auto z = start+u32(bytes_, start+16);
            extra.z.assign(bytes_.begin()+z, bytes_.begin()+z+pixels);
        }
        result.extra = std::move(extra);
    }
    return result;
}
} // namespace ra2::render
