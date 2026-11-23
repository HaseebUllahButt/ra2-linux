#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra2::rules {

// Owning, ordered representation of the text-only INI reader. Names are case
// sensitive, matching the original byte CRC lookup. No Windows/game dependencies.
class IniFile {
public:
    struct Entry {
        std::string key;
        std::string value;
        std::size_t line = 0;
    };
    struct Section {
        std::string name;
        std::vector<Entry> entries;
    };
    struct Statistics {
        std::size_t lines = 0;
        std::size_t section_headers = 0;
        std::size_t entries = 0;
        std::size_t empty_entries = 0;
        std::size_t ignored_lines = 0;
        std::size_t duplicate_sections = 0;
        std::size_t duplicate_keys = 0;
    };

    // Replace on success; I/O failure leaves the old document intact. Syntax
    // noise is ignored as in Westwood's reader. Throws std::runtime_error on I/O.
    static IniFile from_file(const std::string& path);
    void load_file(const std::string& path);
    void parse(std::istream& input);
    void parse(std::string_view text);

    const std::vector<Section>& sections() const noexcept { return sections_; }
    const Statistics& statistics() const noexcept { return statistics_; }
    std::size_t section_count() const noexcept { return sections_.size(); }
    std::size_t key_count(std::string_view section) const;
    const Section* find_section(std::string_view section) const;
    const Entry* find(std::string_view section, std::string_view key) const;
    const Entry* key_at(std::string_view section, std::size_t index) const;
    bool contains(std::string_view section, std::string_view key) const;

    std::string read_string(std::string_view section, std::string_view key,
                            std::string_view fallback = {}) const;
    std::int32_t read_integer(std::string_view section, std::string_view key,
                              std::int32_t fallback = 0) const;
    double read_double(std::string_view section, std::string_view key,
                       double fallback = 0.0) const;
    bool read_bool(std::string_view section, std::string_view key,
                   bool fallback = false) const;
    std::vector<std::string> read_list(std::string_view section,
                                      std::string_view key) const;

    // Public conversion helpers also serve the rules schema. Failed/overflowing
    // conversions are defined safely instead of reproducing original UB.
    static std::string_view trim(std::string_view value) noexcept;
    static std::optional<std::int32_t> integer_value(std::string_view value);
    static std::optional<double> double_value(std::string_view value);
    static std::optional<bool> bool_value(std::string_view value);
    static std::vector<std::string> split_list(std::string_view value);

private:
    void parse_into(std::istream& input);
    std::vector<Section> sections_;
    std::unordered_map<std::string, std::size_t> section_index_;
    std::vector<std::unordered_map<std::string, std::size_t>> entry_index_;
    Statistics statistics_;
};

} // namespace ra2::rules
