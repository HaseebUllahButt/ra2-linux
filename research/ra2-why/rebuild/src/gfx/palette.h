#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ra2::gfx {
class GraphicsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

class Palette {
public:
    static constexpr std::size_t ByteSize = 768;
    // Westwood PAL: 256 RGB triples. Channels expand by byte(channel << 2),
    // exactly as 0x00545150 / 0x005349C0, without 6-to-8-bit replication.
    static Palette from_bytes(const std::uint8_t* bytes, std::size_t size);
    static Palette from_bytes(const std::vector<std::uint8_t>& bytes) {
        return from_bytes(bytes.data(), bytes.size());
    }
    static Palette load_file(const std::filesystem::path& path);
    const Rgba& operator[](std::uint8_t index) const noexcept { return colors_[index]; }
private:
    std::array<Rgba, 256> colors_{};
};
} // namespace ra2::gfx
