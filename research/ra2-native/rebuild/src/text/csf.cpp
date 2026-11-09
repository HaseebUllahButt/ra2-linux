#include "csf.h"
#include <cstring>
#include <stdexcept>

namespace ra2text {

namespace {

uint32_t read_u32(const uint8_t*& p, const uint8_t* end) {
    if (end - p < 4) {
        throw std::runtime_error("csf: truncated integer");
    }
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    p += 4;
    return v;
}

int32_t read_i32(const uint8_t*& p, const uint8_t* end) {
    return static_cast<int32_t>(read_u32(p, end));
}

std::string utf16le_xor_ff_to_utf8(const uint8_t* p, size_t bytes) {
    // Each 16-bit unit is stored XOR 0xFF, little endian.
    std::string out;
    out.reserve(bytes / 2);
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        const uint32_t unit = static_cast<uint32_t>((p[i] ^ 0xFF)) |
            (static_cast<uint32_t>((p[i + 1] ^ 0xFF)) << 8);
        if (unit < 0x80) {
            out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return out;
}

} // namespace

void CsfFile::parse(const uint8_t* data, size_t size) {
    const uint8_t* p = data;
    const uint8_t* end = data + size;

    if (end - p < 4 || std::memcmp(p, " FSC", 4) != 0) {
        throw std::runtime_error("csf: bad magic");
    }
    p += 4;
    const uint32_t version = read_u32(p, end);
    if (version != 3) {
        throw std::runtime_error("csf: unsupported version");
    }
    const uint32_t num_labels = read_u32(p, end);
    string_count_ = read_u32(p, end);
    read_u32(p, end); // unused
    read_u32(p, end); // language

    labels_.reserve(num_labels);
    for (uint32_t i = 0; i < num_labels; ++i) {
        if (end - p < 8 || std::memcmp(p, " LBL", 4) != 0) {
            throw std::runtime_error("csf: bad label marker");
        }
        p += 4;
        const uint32_t value_count = read_u32(p, end);
        const int32_t name_len = read_i32(p, end);
        if (name_len < 0 || end - p < name_len) {
            throw std::runtime_error("csf: bad label name");
        }
        CsfLabel label;
        label.name.assign(reinterpret_cast<const char*>(p),
                          static_cast<size_t>(name_len));
        p += name_len;

        label.values.reserve(value_count);
        for (uint32_t v = 0; v < value_count; ++v) {
            CsfValue value;
            bool wide = false;
            if (end - p >= 4 && std::memcmp(p, " RTS", 4) == 0) {
                p += 4;
            } else if (end - p >= 4 && std::memcmp(p, "WRTS", 4) == 0) {
                p += 4;
                wide = true;
            } else {
                throw std::runtime_error("csf: bad value marker");
            }
            const int32_t chars = read_i32(p, end);
            if (chars < 0 || end - p < chars * 2) {
                throw std::runtime_error("csf: bad value length");
            }
            value.text = utf16le_xor_ff_to_utf8(p,
                                                static_cast<size_t>(chars) * 2);
            p += static_cast<size_t>(chars) * 2;
            if (wide) {
                // Wide entries carry an extra audio-id block:
                // int32 extraLen, then extraLen bytes.
                const int32_t extra_len = read_i32(p, end);
                if (extra_len < 0 || end - p < extra_len) {
                    throw std::runtime_error("csf: bad extra length");
                }
                value.extra.assign(reinterpret_cast<const char*>(p),
                                   static_cast<size_t>(extra_len));
                p += extra_len;
            }
            label.values.push_back(std::move(value));
        }
        labels_.push_back(std::move(label));
    }
}

const CsfValue* CsfFile::find(const std::string& label) const {
    for (const CsfLabel& l : labels_) {
        if (l.name == label && !l.values.empty()) {
            return &l.values[0];
        }
    }
    return nullptr;
}

} // namespace ra2text
