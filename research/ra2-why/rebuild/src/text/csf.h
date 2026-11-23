// Westwood CSF string table reader - the game's text.
// Layout verified from the shipped files (see scripts/csf_dump.py):
//   header: " FSC" + 5x int32 LE: version(3), numLabels, numStrings, 0, 0
//   label:  " LBL" + int32 stringCount + int32 nameLen + name bytes
//   value:  " RTS" = XOR-0xFF UTF-16 chars
//           "WRTS" = same + int32 extraLen + extra bytes (audio id)
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ra2text {

struct CsfValue {
    std::string text;   // UTF-8
    std::string extra;  // audio id, when present
};

struct CsfLabel {
    std::string name;
    std::vector<CsfValue> values;
};

class CsfFile {
public:
    // Parses `data`; throws std::runtime_error on malformed input.
    void parse(const uint8_t* data, size_t size);

    const std::vector<CsfLabel>& labels() const { return labels_; }
    size_t string_count() const { return string_count_; }

    // First value for a label name, or nullptr.
    const CsfValue* find(const std::string& label) const;

private:
    std::vector<CsfLabel> labels_;
    // Name -> first label index with a value, so find() needs no scan.
    std::unordered_map<std::string, size_t> first_;
    size_t string_count_ = 0;
};

} // namespace ra2text
