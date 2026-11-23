#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::audio {
struct Pcm {
    std::uint32_t sample_rate;
    std::uint16_t channels;
    std::vector<std::int16_t> samples;
};
class Bag {
public:
    static Bag open(std::vector<std::uint8_t> index, std::vector<std::uint8_t> bag);
    Pcm decode(std::string_view name) const;
private:
    struct Entry { std::string name; std::uint32_t offset, size, rate, flags, extra; };
    std::vector<Entry> entries_;
    std::vector<std::uint8_t> bag_;
};
Pcm decode_wav(const std::vector<std::uint8_t>& bytes);
Pcm decode_aud(const std::vector<std::uint8_t>& bytes);
}

namespace ra2::rules { class IniFile; }
namespace ra2::audio {
struct SoundEvent {
    std::vector<std::string> samples;
};
struct Theme {
    std::string sound;
    int scenario;
    bool normal;
    bool repeat;
};
// The caller supplies the selected original INI and controls sample selection.
std::optional<SoundEvent> find_sound(const rules::IniFile& ini, std::string_view name);
std::optional<Theme> find_theme(const rules::IniFile& ini, std::string_view name);
}
