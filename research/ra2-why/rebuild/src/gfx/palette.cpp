#include "palette.h"

#include <fstream>

namespace ra2::gfx {
Palette Palette::from_bytes(const std::uint8_t* bytes, std::size_t size) {
    if (size != ByteSize || bytes == nullptr)
        throw GraphicsError("PAL must contain exactly 768 bytes");
    Palette palette;
    for (std::size_t i = 0; i < 256; ++i) {
        palette.colors_[i] = {static_cast<std::uint8_t>(unsigned(bytes[3 * i]) << 2),
                              static_cast<std::uint8_t>(unsigned(bytes[3 * i + 1]) << 2),
                              static_cast<std::uint8_t>(unsigned(bytes[3 * i + 2]) << 2), 255};
    }
    return palette;
}

Palette Palette::load_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw GraphicsError("Cannot open PAL: " + path.string());
    // One extra byte detects oversized files without allocating their contents.
    std::array<std::uint8_t, ByteSize + 1> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (input.bad()) throw GraphicsError("Cannot read PAL: " + path.string());
    return from_bytes(bytes.data(), static_cast<std::size_t>(input.gcount()));
}
} // namespace ra2::gfx
