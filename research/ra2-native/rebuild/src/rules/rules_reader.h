#pragma once

#include "ini_file.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ra2::rules {

using RuleValue = std::variant<std::monostate, std::int32_t, bool, double,
                               std::vector<std::int32_t>>;
enum class RuleKind { Integer, Byte, Boolean, Float, Double, IntegerList };

struct GeneralField {
    const char* key;
    RuleKind kind;
    std::uint32_t original_offset; // Evidence only; never used for memory access.
    RuleValue initial_value;       // monostate means constructor default unproven.
    std::uint32_t fallback_offset; // Usually original_offset; see CMislAcceleration.
};

struct GeneralReadReport {
    bool section_present = false;
    std::size_t loaded = 0;
    std::size_t retained = 0;
    std::vector<std::string> invalid_keys;
    std::vector<std::string> unhandled_keys;
};

class RulesReader {
public:
    // Recovered constructor literals for the supported slice of 0x665650.
    RulesReader();
    // Portable convenience instance, corresponding to storage at 0x8871E0.
    // Applications may also own independent instances (recommended for tests).
    static RulesReader& instance();
    static const std::vector<GeneralField>& general_schema();

    // First-file reset followed by the supported Read_File slice. No type
    // registries, LANGRULE file discovery, or unrelated sections are emulated.
    GeneralReadReport init(const IniFile& ini);
    GeneralReadReport read_file(const IniFile& ini);
    GeneralReadReport read_general(const IniFile& ini);

    const std::map<std::string, RuleValue, std::less<>>& general() const noexcept {
        return general_;
    }
    template<class T> std::optional<T> get(std::string_view key) const {
        const auto it = general_.find(key);
        if (it == general_.end()) return std::nullopt;
        const auto* value = std::get_if<T>(&it->second);
        return value ? std::optional<T>{*value} : std::nullopt;
    }

private:
    std::map<std::string, RuleValue, std::less<>> general_;
};

} // namespace ra2::rules
