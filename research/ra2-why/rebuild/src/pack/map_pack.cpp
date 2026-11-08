#include "map_pack.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace ra2::pack {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
std::uint16_t word(const Bytes& b, std::size_t i) {
    if (i > b.size() || b.size() - i < 2) fail("Truncated pack word");
    return b[i] | (std::uint16_t(b[i + 1]) << 8);
}
std::uint32_t dword(const Bytes& b, std::size_t i) {
    return word(b, i) | (std::uint32_t(word(b, i + 2)) << 16);
}

// Use the Linux LZO library for the standard codec, with bounded output.
// RA2's chunk framing and terrain records are implemented here.
class Lzo {
    void* library_ = nullptr;
    using Decode = int (*)(const unsigned char*, std::size_t, unsigned char*,
                           std::size_t*, void*);
    Decode decode_ = nullptr;
public:
    Lzo() {
        library_ = dlopen("liblzo2.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!library_) fail("Install liblzo2 to read original map terrain");
        void* symbol = dlsym(library_, "lzo1x_decompress_safe");
        static_assert(sizeof(symbol) == sizeof(decode_));
        std::memcpy(&decode_, &symbol, sizeof(decode_));
        if (!decode_) {
            dlclose(library_);
            fail("liblzo2 has no safe LZO1X decoder");
        }
    }
    ~Lzo() { dlclose(library_); }
    Lzo(const Lzo&) = delete;
    Lzo& operator=(const Lzo&) = delete;
    Bytes decode(const Bytes& input, std::size_t expected) const {
        Bytes output(expected);
        std::size_t size = expected;
        const int status = decode_(input.data(), input.size(), output.data(), &size, nullptr);
        if (status != 0 || size != expected) fail("Invalid LZO map block");
        return output;
    }
};
}

Bytes decode_base64(std::string_view text) {
    std::array<int, 256> table;
    table.fill(-1);
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t i = 0; i < alphabet.size(); ++i)
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    std::string clean;
    for (unsigned char c : text) {
        if (!std::isspace(c)) clean.push_back(static_cast<char>(c));
    }
    if (clean.size() % 4) fail("Truncated base64 map section");
    Bytes out;
    for (std::size_t i = 0; i < clean.size(); i += 4) {
        const bool pad2 = clean[i + 2] == '=', pad3 = clean[i + 3] == '=';
        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != clean.size()))
            fail("Invalid base64 padding");
        std::array<unsigned, 4> q{};
        for (unsigned j = 0; j < 4; ++j) {
            if (j >= 2 && clean[i + j] == '=') continue;
            const int value = table[static_cast<unsigned char>(clean[i + j])];
            if (value < 0) fail("Invalid base64 map byte");
            q[j] = static_cast<unsigned>(value);
        }
        if ((pad2 && (q[1] & 15)) || (pad3 && !pad2 && (q[2] & 3)))
            fail("Nonzero base64 padding bits");
        out.push_back(static_cast<std::uint8_t>((q[0] << 2) | (q[1] >> 4)));
        if (!pad2) out.push_back(static_cast<std::uint8_t>((q[1] << 4) | (q[2] >> 2)));
        if (!pad3) out.push_back(static_cast<std::uint8_t>((q[2] << 6) | q[3]));
    }
    return out;
}

// Original 0x00551C60. Copy one byte at a time: overlapping runs repeat.
Bytes decode_lcw(const Bytes& input, std::size_t expected) {
    Bytes out;
    out.reserve(expected);
    std::size_t pos = 0;
    auto byte = [&]() -> unsigned {
        if (pos == input.size()) fail("Truncated LCW command");
        return input[pos++];
    };
    auto next_word = [&]() -> std::size_t {
        const unsigned low = byte();
        return low | (byte() << 8);
    };
    auto room = [&](std::size_t count) {
        if (count > expected - out.size()) fail("LCW block exceeds declared size");
    };
    auto copy = [&](std::size_t source, std::size_t count) {
        room(count);
        if (source >= out.size() && count) fail("Invalid LCW copy source");
        for (std::size_t i = 0; i < count; ++i) out.push_back(out[source + i]);
    };
    while (pos < input.size()) {
        const unsigned command = byte();
        if (command == 0x80) {
            if (pos != input.size() || out.size() != expected)
                fail("LCW terminator or output size mismatch");
            return out;
        }
        if (!(command & 0x80)) {
            const std::size_t distance = ((command & 15) << 8) | byte();
            if (!distance || distance > out.size()) fail("Invalid LCW back reference");
            copy(out.size() - distance, (command >> 4) + 3);
        } else if (!(command & 0x40)) {
            const std::size_t count = command & 63;
            room(count);
            if (count > input.size() - pos) fail("Truncated LCW literal");
            out.insert(out.end(), input.begin() + pos, input.begin() + pos + count);
            pos += count;
        } else if (command == 0xfe) {
            const std::size_t count = next_word();
            const auto value = static_cast<std::uint8_t>(byte());
            room(count);
            out.insert(out.end(), count, value);
        } else {
            const std::size_t count = command == 0xff ? next_word() : (command & 63) + 3;
            const std::size_t source = next_word();
            copy(source, count);
        }
    }
    fail("Missing LCW terminator");
}

Bytes decode_chunks(const Bytes& input, Codec codec, std::size_t limit) {
    Bytes output;
    std::size_t pos = 0;
    while (pos < input.size()) {
        const std::size_t compressed = word(input, pos), expanded = word(input, pos + 2);
        pos += 4;
        if (!compressed || !expanded || expanded > 8192) fail("Invalid map block sizes");
        if (compressed > input.size() - pos) fail("Truncated compressed map block");
        if (expanded > limit - output.size()) fail("Map section exceeds output limit");
        Bytes block(input.begin() + pos, input.begin() + pos + compressed);
        Bytes decoded;
        if (codec == Codec::Lcw) decoded = decode_lcw(block, expanded);
        else {
            static const Lzo lzo;
            decoded = lzo.decode(block, expanded);
        }
        output.insert(output.end(), decoded.begin(), decoded.end());
        pos += compressed;
    }
    return output;
}

Bytes read_section(const rules::IniFile& ini, std::string_view section, Codec codec) {
    const auto* found = ini.find_section(section);
    if (!found) return {};
    std::vector<std::pair<std::int32_t, std::string_view>> lines;
    for (const auto& entry : found->entries) {
        const auto number = rules::IniFile::integer_value(entry.key);
        if (!number || *number < 1) fail("Invalid numbered map pack line");
        lines.emplace_back(*number, entry.value);
    }
    std::sort(lines.begin(), lines.end());
    std::string text;
    std::int32_t expected = 1;
    for (const auto& [number, value] : lines) {
        if (number != expected++) fail("Missing or duplicate map pack line");
        if (value.size() > 32 * 1024 * 1024 - text.size()) fail("Map pack text too large");
        text += value;
    }
    return decode_chunks(decode_base64(text), codec);
}

Terrain load_terrain(const rules::IniFile& ini) {
    const Bytes bytes = read_section(ini, "IsoMapPack5", Codec::Lzo);
    if (bytes.size() < 4 || (bytes.size() - 4) % 11 || dword(bytes, bytes.size() - 4) != 0)
        fail("Invalid terrain records or missing coordinate terminator");
    Terrain out;
    out.tiles.reserve((bytes.size() - 4) / 11);
    for (std::size_t i = 0; i + 4 < bytes.size(); i += 11) {
        Tile t{word(bytes, i), word(bytes, i + 2), dword(bytes, i + 4),
               bytes[i + 8], bytes[i + 9], bytes[i + 10]};
        if (t.x >= 512 || t.y >= 512 || (!t.x && !t.y)) fail("Invalid terrain cell coordinates");
        out.tiles.push_back(t);
    }
    out.overlay = read_section(ini, "OverlayPack", Codec::Lcw);
    out.overlay_data = read_section(ini, "OverlayDataPack", Codec::Lcw);
    for (const auto* overlay : {&out.overlay, &out.overlay_data}) {
        if (!overlay->empty() && overlay->size() != 512 * 512)
            fail("Overlay does not cover the original 512 by 512 cell grid");
    }
    return out;
}
} // namespace ra2::pack
