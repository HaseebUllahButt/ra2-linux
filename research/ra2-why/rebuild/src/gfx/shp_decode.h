#pragma once

#include "palette.h"

namespace ra2::gfx {
struct ShpHeader {
    std::uint16_t width = 0, height = 0, frame_count = 0;
};
struct ShpFrame {
    std::int16_t x = 0, y = 0;
    std::uint16_t width = 0, height = 0;
    std::uint8_t flags = 0; // Only bit 0x02 selects the scanline decoder.
    std::array<std::uint8_t, 3> radar_color{}; // Record bytes 12..14, unscaled.
    std::uint32_t data_offset = 0; // Absolute byte offset in the SHP blob.
    bool scanline_rle() const noexcept { return (flags & 2) != 0; }
};
struct RgbaImage {
    std::uint16_t width = 0, height = 0;
    std::vector<std::uint8_t> pixels; // Row-major R,G,B,A; width * height * 4.
};

// Owns the blob; frame metadata and subsequent decodes remain valid after the
// source archive or input vector goes away. No packed structs or unaligned loads.
class ShpImage {
public:
    static constexpr std::size_t DefaultMaxPixels = 16 * 1024 * 1024;
    static ShpImage parse(std::vector<std::uint8_t> bytes,
                          std::size_t max_pixels = DefaultMaxPixels);
    const ShpHeader& header() const noexcept { return header_; }
    const std::vector<ShpFrame>& frames() const noexcept { return frames_; }
    // Full nominal canvas. Crop offsets are signed and clipped to the canvas.
    // Index zero and skipped pixels have RGBA (0,0,0,0). No game lighting,
    // remapping, shadow blending, or compositing is applied.
    RgbaImage decode_frame(std::size_t index, const Palette& palette) const;
private:
    ShpHeader header_;
    std::vector<ShpFrame> frames_;
    std::vector<std::size_t> frame_ends_;
    std::vector<std::uint8_t> bytes_;
};
} // namespace ra2::gfx
