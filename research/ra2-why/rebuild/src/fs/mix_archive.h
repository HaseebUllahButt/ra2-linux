#pragma once

#include "filename_hash.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::fs {
class MixError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct MixEntry {
    std::uint32_t id;
    std::uint32_t offset; // Relative to the archive's data section.
    std::uint32_t size;
};

// Immutable directory; reads reopen the backing file in binary, read-only mode.
// Nested archives share a bounded backing-file range, never a payload copy.
class MixArchive {
public:
    static constexpr std::uint32_t Checksum = 0x00010000;
    static constexpr std::uint32_t Encrypted = 0x00020000;
    static std::shared_ptr<MixArchive> open(const std::filesystem::path& path);
    // Owned bytes are useful for embedded archives and deterministic tests.
    static std::shared_ptr<MixArchive> from_bytes(std::vector<std::uint8_t> bytes,
                                                 std::string name = "<memory>");
    std::shared_ptr<MixArchive> open_nested(const MixEntry& entry, std::string name) const;
    const MixEntry* find(std::uint32_t id) const noexcept;
    const MixEntry* find(std::string_view filename,
                         FilenameHash type = FilenameHash::Crc32) const noexcept;
    std::vector<std::uint8_t> read(const MixEntry& entry) const;
    const std::vector<MixEntry>& entries() const noexcept { return entries_; }
    const std::string& name() const noexcept { return name_; }
    std::uint32_t flags() const noexcept { return flags_; }
    bool is_classic() const noexcept { return classic_; }
    bool encrypted() const noexcept { return (flags_ & Encrypted) != 0; }
    bool has_checksum() const noexcept { return (flags_ & Checksum) != 0; }
    std::uint32_t data_size() const noexcept { return data_size_; }
    std::uint64_t data_offset() const noexcept { return data_offset_; }
    std::uint64_t backing_offset() const noexcept { return base_; }
private:
    struct Backing;
    MixArchive(std::shared_ptr<const Backing> backing, std::uint64_t base,
               std::uint64_t length, std::string name);
    void parse();
    std::vector<std::uint8_t> read_range(std::uint64_t offset, std::size_t size) const;
    void check_entry(const MixEntry& entry) const;
    std::shared_ptr<const Backing> backing_;
    std::uint64_t base_ = 0, length_ = 0, data_offset_ = 0;
    std::uint32_t flags_ = 0, data_size_ = 0;
    bool classic_ = false;
    std::string name_;
    std::vector<MixEntry> entries_;
    std::vector<std::size_t> lookup_; // Sorted independently; retain on-disk order.
};
} // namespace ra2::fs
