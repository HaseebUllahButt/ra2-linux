#include "ini_file.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ra2::rules {

std::string_view IniFile::trim(std::string_view value) noexcept {
    while (!value.empty() && static_cast<unsigned char>(value.front()) <= 0x20)
        value.remove_prefix(1);
    while (!value.empty() && static_cast<unsigned char>(value.back()) <= 0x20)
        value.remove_suffix(1);
    return value;
}

IniFile IniFile::from_file(const std::string& path) {
    IniFile result;
    result.load_file(path);
    return result;
}

void IniFile::load_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open INI: " + path);
    parse(input);
}

void IniFile::parse(std::string_view text) {
    std::istringstream input{std::string(text)};
    parse(input);
}

void IniFile::parse(std::istream& input) {
    IniFile next;
    next.parse_into(input);
    *this = std::move(next);
}

void IniFile::parse_into(std::istream& input) {
    Section pending;
    bool in_section = false;
    auto finish_section = [&] {
        // 0x525A60 discards sections with no entries when comments are not kept.
        if (!in_section || pending.entries.empty()) return;
        const auto index = sections_.size();
        if (!section_index_.emplace(pending.name, index).second)
            ++statistics_.duplicate_sections;
        std::unordered_map<std::string, std::size_t> keys;
        for (std::size_t i = 0; i < pending.entries.size(); ++i) {
            if (!keys.emplace(pending.entries[i].key, i).second)
                ++statistics_.duplicate_keys;
        }
        sections_.push_back(std::move(pending));
        entry_index_.push_back(std::move(keys));
    };

    std::string line;
    while (std::getline(input, line)) {
        ++statistics_.lines;
        // 0x65D5C0 discards CR bytes and terminates each physical line at LF.
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        std::string_view text(line);
        const auto nul = text.find('\0');
        if (nul != std::string_view::npos) text = text.substr(0, nul);
        text = trim(text);
        if (!text.empty() && text.front() == '[') {
            const auto close = text.find(']');
            if (close != std::string_view::npos) {
                finish_section();
                pending = Section{std::string(text.substr(1, close - 1)), {}};
                in_section = true;
                ++statistics_.section_headers;
                continue;
            }
        }
        // Quotes do not protect semicolons; '#' is ordinary text.
        text = trim(text.substr(0, text.find(';')));
        if (text.empty()) continue;
        const auto equal = text.find('=');
        if (!in_section || equal == std::string_view::npos) {
            ++statistics_.ignored_lines;
            continue;
        }
        const auto key = trim(text.substr(0, equal));
        const auto value = trim(text.substr(equal + 1));
        if (key.empty() || value.empty()) {
            ++statistics_.empty_entries;
            continue;
        }
        pending.entries.push_back({std::string(key), std::string(value), statistics_.lines});
        ++statistics_.entries;
    }
    if (input.bad() || (input.fail() && !input.eof()))
        throw std::runtime_error("Failed while reading INI stream");
    finish_section();
}

const IniFile::Section* IniFile::find_section(std::string_view section) const {
    const auto it = section_index_.find(std::string(section));
    return it == section_index_.end() ? nullptr : &sections_[it->second];
}

const IniFile::Entry* IniFile::find(std::string_view section, std::string_view key) const {
    const auto s = section_index_.find(std::string(section));
    if (s == section_index_.end()) return nullptr;
    const auto& keys = entry_index_[s->second];
    const auto k = keys.find(std::string(key));
    return k == keys.end() ? nullptr : &sections_[s->second].entries[k->second];
}

const IniFile::Entry* IniFile::key_at(std::string_view section, std::size_t index) const {
    const auto* s = find_section(section);
    return s && index < s->entries.size() ? &s->entries[index] : nullptr;
}

std::size_t IniFile::key_count(std::string_view section) const {
    const auto* s = find_section(section);
    return s ? s->entries.size() : 0;
}

bool IniFile::contains(std::string_view section, std::string_view key) const {
    return find(section, key) != nullptr;
}

std::string IniFile::read_string(std::string_view section, std::string_view key,
                                 std::string_view fallback) const {
    const auto* entry = find(section, key);
    return std::string(trim(entry ? std::string_view(entry->value) : fallback));
}

std::optional<std::int32_t> IniFile::integer_value(std::string_view value) {
    value = trim(value);
    if (value.empty()) return std::nullopt;
    int base = 10;
    if (value.front() == '$') {
        base = 16;
        value.remove_prefix(1);
    } else if (value.back() == 'h' || value.back() == 'H') {
        base = 16;
        value.remove_suffix(1);
    }
    const std::string owned(value);
    std::size_t consumed = 0;
    try {
        // Decimal conversion is atoi-like: numeric prefixes are accepted and
        // non-numeric input yields zero. Hex conversion is scanf-like.
        const auto n = std::stoll(owned, &consumed, base);
        const auto lo = std::numeric_limits<std::int32_t>::min();
        const auto hi = base == 16 ? std::int64_t{0xffffffffu}
                                   : std::int64_t{std::numeric_limits<std::int32_t>::max()};
        if (n < lo || n > hi) return std::nullopt;
        if (n > std::numeric_limits<std::int32_t>::max())
            return static_cast<std::int32_t>(n - 0x100000000LL);
        return static_cast<std::int32_t>(n);
    } catch (const std::invalid_argument&) {
        return base == 10 ? std::optional<std::int32_t>{0} : std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

std::optional<double> IniFile::double_value(std::string_view value) {
    // 0x5283D0 scans a float, promotes to double, then scales '%' by .01.
    std::istringstream input{std::string(trim(value))};
    input.imbue(std::locale::classic());
    float number = 0;
    if (!(input >> number) || !std::isfinite(number)) return std::nullopt;
    double result = number;
    if (value.find('%') != std::string_view::npos) result *= 0.01;
    return result;
}

std::optional<bool> IniFile::bool_value(std::string_view value) {
    value = trim(value);
    if (value.empty()) return std::nullopt;
    switch (value.front()) {
    case '0': case 'f': case 'F': case 'n': case 'N': return false;
    case '1': case 't': case 'T': case 'y': case 'Y': return true;
    default: return std::nullopt;
    }
}

std::int32_t IniFile::read_integer(std::string_view section, std::string_view key,
                                   std::int32_t fallback) const {
    const auto* e = find(section, key);
    return e ? integer_value(e->value).value_or(fallback) : fallback;
}

double IniFile::read_double(std::string_view section, std::string_view key,
                            double fallback) const {
    const auto* e = find(section, key);
    return e ? double_value(e->value).value_or(fallback) : fallback;
}

bool IniFile::read_bool(std::string_view section, std::string_view key, bool fallback) const {
    const auto* e = find(section, key);
    return e ? bool_value(e->value).value_or(fallback) : fallback;
}

std::vector<std::string> IniFile::split_list(std::string_view value) {
    std::vector<std::string> result;
    while (!value.empty()) {
        const auto comma = value.find(',');
        const auto token = trim(value.substr(0, comma));
        if (!token.empty()) result.emplace_back(token);
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
    }
    return result;
}

std::vector<std::string> IniFile::read_list(std::string_view section, std::string_view key) const {
    const auto* e = find(section, key);
    return e ? split_list(e->value) : std::vector<std::string>{};
}

} // namespace ra2::rules
