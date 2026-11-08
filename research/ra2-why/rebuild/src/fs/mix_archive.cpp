#include "mix_archive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>

namespace ra2::fs {
namespace {
std::uint16_t le16(const std::uint8_t* p) {
    return std::uint16_t(p[0]) | (std::uint16_t(p[1]) << 8);
}
std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
void put_be32(std::uint8_t* p, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}

// Standard Blowfish ECB. The original 0x00438210 stream calls the COM
// cryptography interface at 0x00438030; this replaces that Windows dependency.
class Blowfish {
public:
    explicit Blowfish(const std::array<std::uint8_t, 56>& key) {
        static constexpr std::uint32_t initial[1042] = {
#include "blowfish_constants.inc"
        };
        std::copy_n(initial, 18, p_.begin());
        std::copy_n(initial + 18, 1024, s_.begin());
        std::size_t pos = 0;
        for (auto& p : p_) {
            std::uint32_t word = 0;
            for (unsigned b = 0; b < 4; ++b) {
                word = (word << 8) | key[pos];
                pos = (pos + 1) % key.size();
            }
            p ^= word;
        }
        std::uint32_t left = 0, right = 0;
        for (std::size_t i = 0; i < p_.size(); i += 2) {
            encrypt(left, right); p_[i] = left; p_[i + 1] = right;
        }
        for (std::size_t i = 0; i < s_.size(); i += 2) {
            encrypt(left, right); s_[i] = left; s_[i + 1] = right;
        }
    }
    void decrypt(std::vector<std::uint8_t>& bytes) const {
        if (bytes.size() % 8) throw MixError("Blowfish input is not block aligned");
        for (std::size_t i = 0; i < bytes.size(); i += 8) {
            auto left = be32(bytes.data() + i), right = be32(bytes.data() + i + 4);
            for (int round = 17; round > 1; --round) {
                left ^= p_[round]; right ^= f(left); std::swap(left, right);
            }
            std::swap(left, right); right ^= p_[1]; left ^= p_[0];
            put_be32(bytes.data() + i, left); put_be32(bytes.data() + i + 4, right);
        }
    }
private:
    std::uint32_t f(std::uint32_t v) const {
        return ((s_[v >> 24] + s_[256 + ((v >> 16) & 255)]) ^
                 s_[512 + ((v >> 8) & 255)]) + s_[768 + (v & 255)];
    }
    void encrypt(std::uint32_t& left, std::uint32_t& right) const {
        for (unsigned round = 0; round < 16; ++round) {
            left ^= p_[round]; right ^= f(left); std::swap(left, right);
        }
        std::swap(left, right); right ^= p_[16]; left ^= p_[17];
    }
    std::array<std::uint32_t, 18> p_{};
    std::array<std::uint32_t, 1024> s_{};
};

// Small fixed-width arithmetic suffices for Westwood's 320-bit public key.
// An extra limb keeps addition exact before modular reduction; no OpenSSL,
// Boost, compiler-specific integers, or external linker flags are required.
using Integer = std::array<std::uint32_t, 11>;
bool less(const Integer& a, const Integer& b) {
    for (std::size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i]) return a[i] < b[i];
    return false;
}
void subtract(Integer& a, const Integer& b) {
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::uint64_t sub = std::uint64_t(b[i]) + borrow;
        const auto old = a[i]; a[i] = static_cast<std::uint32_t>(old - sub);
        borrow = std::uint64_t(old) < sub;
    }
}
Integer add_mod(const Integer& a, const Integer& b, const Integer& modulus) {
    Integer result{};
    std::uint64_t carry = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto sum = std::uint64_t(a[i]) + b[i] + carry;
        result[i] = static_cast<std::uint32_t>(sum); carry = sum >> 32;
    }
    if (!less(result, modulus)) subtract(result, modulus);
    return result;
}
Integer multiply_mod(Integer a, const Integer& b, const Integer& modulus) {
    Integer result{};
    for (unsigned bit = 0; bit < 320; ++bit) {
        if ((b[bit / 32] >> (bit % 32)) & 1u) result = add_mod(result, a, modulus);
        a = add_mod(a, a, modulus);
    }
    return result;
}
std::array<std::uint8_t, 56> decode_key(const std::vector<std::uint8_t>& block) {
    // A compile-time public modulus keeps the arithmetic independent of OS or
    // crypto libraries. The format constant is supplied as 40 big-endian bytes.
#ifdef RA2_MIX_PUBLIC_MODULUS
    static constexpr std::uint8_t modulus_bytes[40] = {RA2_MIX_PUBLIC_MODULUS};
#else
    // The original code's public-key bytes are not in the supplied function
    // decompilations. Fail explicitly until the format constant is available.
    static constexpr std::uint8_t modulus_bytes[40] = {};
#endif
    Integer modulus{};
    for (unsigned i = 0; i < 40; ++i)
        modulus[i / 4] |= std::uint32_t(modulus_bytes[39 - i]) << (8 * (i % 4));
    if (modulus == Integer{}) throw MixError("MIX public modulus is not configured");
    std::array<std::uint8_t, 56> key{};
    for (unsigned offset = 0; offset < 80; offset += 40) {
        Integer value{};
        for (unsigned i = 0; i < 40; ++i)
            value[i / 4] |= std::uint32_t(block[offset + i]) << (8 * (i % 4));
        // Accept the same modular input domain as the original exponentiation.
        while (!less(value, modulus)) subtract(value, modulus);
        Integer result{}; result[0] = 1;
        for (std::uint32_t exponent = 65537; exponent; exponent >>= 1) {
            if (exponent & 1u) result = multiply_mod(result, value, modulus);
            value = multiply_mod(value, value, modulus);
        }
        for (unsigned i = 0; i < 39 && offset / 40 * 39 + i < key.size(); ++i)
            key[offset / 40 * 39 + i] = static_cast<std::uint8_t>(result[i / 4] >> (8 * (i % 4)));
    }
    return key;
}
} // namespace

struct MixArchive::Backing {
    std::filesystem::path path;
    std::vector<std::uint8_t> bytes;
    bool memory = false;
};

MixArchive::MixArchive(std::shared_ptr<const Backing> backing, std::uint64_t base,
                       std::uint64_t length, std::string name)
    : backing_(std::move(backing)), base_(base), length_(length), name_(std::move(name)) {
    parse();
}
std::shared_ptr<MixArchive> MixArchive::open(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw MixError(path.string() + ": " + error.message());
    auto backing = std::make_shared<Backing>();
    backing->path = std::filesystem::absolute(path);
    return std::shared_ptr<MixArchive>(new MixArchive(backing, 0, size, path.string()));
}
std::shared_ptr<MixArchive> MixArchive::from_bytes(std::vector<std::uint8_t> bytes, std::string name) {
    auto backing = std::make_shared<Backing>();
    backing->bytes = std::move(bytes); backing->memory = true;
    return std::shared_ptr<MixArchive>(new MixArchive(backing, 0, backing->bytes.size(), std::move(name)));
}
std::vector<std::uint8_t> MixArchive::read_range(std::uint64_t offset, std::size_t size) const {
    if (offset > length_ || size > length_ - offset)
        throw MixError(name_ + ": truncated archive or range outside archive");
    std::vector<std::uint8_t> result(size);
    if (size == 0) return result;
    const auto absolute = base_ + offset;
    if (backing_->memory) {
        std::copy_n(backing_->bytes.data() + absolute, size, result.data());
    } else {
        if (absolute > std::uint64_t(std::numeric_limits<std::streamoff>::max()) ||
            size > std::uint64_t(std::numeric_limits<std::streamsize>::max()))
            throw MixError(name_ + ": file range exceeds stream limits");
        std::ifstream input(backing_->path, std::ios::binary);
        if (!input || !input.seekg(static_cast<std::streamoff>(absolute)) ||
            !input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size)))
            throw MixError(name_ + ": could not read backing file");
    }
    return result;
}

// 0x005B3C20: classic header, or flags + optional RSA key + padded BF header.
void MixArchive::parse() {
    auto prefix = read_range(0, 2);
    classic_ = le16(prefix.data()) != 0;
    const std::uint64_t header_start = classic_ ? 0 : 4;
    if (!classic_) {
        prefix = read_range(0, 4); flags_ = le32(prefix.data());
        if (flags_ & ~(Checksum | Encrypted)) throw MixError(name_ + ": unsupported MIX flags");
    }
    std::vector<std::uint8_t> header;
    if (encrypted()) {
        header = read_range(84, 8);
        const Blowfish cipher(decode_key(read_range(4, 80)));
        cipher.decrypt(header);
        const auto count = le16(header.data());
        const std::size_t padded_size = (6 + 12 * std::size_t(count) + 7) & ~std::size_t(7);
        header = read_range(84, padded_size); cipher.decrypt(header);
        data_offset_ = 84 + padded_size;
    } else {
        header = read_range(header_start, 6);
        const auto count = le16(header.data());
        header = read_range(header_start, 6 + 12 * std::size_t(count));
        data_offset_ = header_start + header.size();
    }
    const auto count = le16(header.data());
    data_size_ = le32(header.data() + 2);
    if (data_offset_ > length_ || data_size_ > length_ - data_offset_)
        throw MixError(name_ + ": declared data size exceeds archive");
    if (has_checksum() && length_ - data_offset_ - data_size_ < 20)
        throw MixError(name_ + ": missing SHA-1 trailer");
    entries_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* p = header.data() + 6 + 12 * i;
        MixEntry entry{le32(p), le32(p + 4), le32(p + 8)};
        check_entry(entry); entries_.push_back(entry);
    }
    lookup_.resize(entries_.size());
    std::iota(lookup_.begin(), lookup_.end(), std::size_t(0));
    std::stable_sort(lookup_.begin(), lookup_.end(), [this](auto a, auto b) {
        return entries_[a].id < entries_[b].id;
    });
}
void MixArchive::check_entry(const MixEntry& entry) const {
    if (entry.offset > data_size_ || entry.size > data_size_ - entry.offset)
        throw MixError(name_ + ": entry exceeds data section");
}
const MixEntry* MixArchive::find(std::uint32_t id) const noexcept {
    const auto found = std::lower_bound(lookup_.begin(), lookup_.end(), id,
        [this](auto index, auto key) { return entries_[index].id < key; });
    return found != lookup_.end() && entries_[*found].id == id ? &entries_[*found] : nullptr;
}
const MixEntry* MixArchive::find(std::string_view filename, FilenameHash type) const noexcept {
    return find(filename_hash(filename, type));
}
std::vector<std::uint8_t> MixArchive::read(const MixEntry& entry) const {
    check_entry(entry); return read_range(data_offset_ + entry.offset, entry.size);
}
std::shared_ptr<MixArchive> MixArchive::open_nested(const MixEntry& entry, std::string name) const {
    check_entry(entry);
    return std::shared_ptr<MixArchive>(new MixArchive(backing_, base_ + data_offset_ + entry.offset,
                                                    entry.size, std::move(name)));
}
} // namespace ra2::fs
