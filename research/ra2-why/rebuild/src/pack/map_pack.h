#pragma once

#include "../rules/ini_file.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ra2::pack {
using Bytes = std::vector<std::uint8_t>;
enum class Codec { Lcw, Lzo };

Bytes decode_base64(std::string_view text);
Bytes decode_lcw(const Bytes& input, std::size_t expected_size);
Bytes decode_chunks(const Bytes& input, Codec codec,
                    std::size_t limit = 16 * 1024 * 1024);
Bytes read_section(const rules::IniFile& ini, std::string_view section, Codec codec);

// Original 0x0056BAC0: coordinates, tile index, subtile, height, ice byte.
struct Tile {
    std::uint16_t x, y;
    std::uint32_t index;
    std::uint8_t subtile, height, ice;
};
struct Terrain {
    std::vector<Tile> tiles;
    // Original 0x005FD2E0: two independent 512 * 512 byte arrays.
    Bytes overlay;
    Bytes overlay_data;
};
Terrain load_terrain(const rules::IniFile& ini);
} // namespace ra2::pack
