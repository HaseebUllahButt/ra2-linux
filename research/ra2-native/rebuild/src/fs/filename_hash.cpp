#include "filename_hash.h"

namespace ra2::fs {
namespace {
unsigned char upper(unsigned char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<unsigned char>(c - 'a' + 'A') : c;
}
std::uint32_t crc_byte(std::uint32_t crc, unsigned char c) noexcept {
    crc ^= c;
    for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    return crc;
}
} // namespace

std::uint32_t classic_filename_hash(std::string_view name) noexcept {
    std::uint32_t hash = 0;
    for (std::size_t pos = 0; pos < name.size();) {
        std::uint32_t word = 0;
        for (unsigned byte = 0; byte < 4 && pos < name.size(); ++byte, ++pos)
            word |= std::uint32_t(upper(static_cast<unsigned char>(name[pos]))) << (8 * byte);
        hash = ((hash << 1) | (hash >> 31)) + word;
    }
    return hash;
}

// 0x004A1DE0, with CRC update 0x004A1FB0 and uppercase at 0x007DCFC4.
// A partial word gets its length byte, then copies of that word's first byte.
std::uint32_t crc_filename_hash(std::string_view name) noexcept {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char c : name)
        crc = crc_byte(crc, upper(c));
    const auto remainder = name.size() % 4;
    if (remainder) {
        crc = crc_byte(crc, static_cast<unsigned char>(remainder));
        for (auto i = remainder + 1; i < 4; ++i)
            crc = crc_byte(crc, upper(static_cast<unsigned char>(name[name.size() - remainder])));
    }
    return ~crc;
}
std::uint32_t filename_hash(std::string_view name, FilenameHash type) noexcept {
    return type == FilenameHash::Classic ? classic_filename_hash(name) : crc_filename_hash(name);
}
} // namespace ra2::fs
