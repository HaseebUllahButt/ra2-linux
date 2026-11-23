#pragma once

#include <cstdint>
#include <string_view>

namespace ra2::fs {
enum class FilenameHash { Classic, Crc32 };

// ASCII case folding only; archive names are byte strings, not locale text.
// Directory separators are preserved, as in the original engine.
std::uint32_t classic_filename_hash(std::string_view filename) noexcept;
std::uint32_t crc_filename_hash(std::string_view filename) noexcept;
std::uint32_t filename_hash(std::string_view filename, FilenameHash type) noexcept;
} // namespace ra2::fs
