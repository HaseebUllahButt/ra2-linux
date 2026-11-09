#include "audio.h"
#include "../rules/ini_file.h"

namespace ra2::audio {
std::optional<SoundEvent> find_sound(const rules::IniFile& ini, std::string_view name) {
    if (!ini.find_section(name)) return std::nullopt;
    // 0x00750440 reads the Sounds string and tokenizes its sample names.
    const auto value = ini.read_string(name, "Sounds");
    SoundEvent event;
    std::size_t i = 0;
    const auto delimiter = [](char c) { return c == ',' || static_cast<unsigned char>(c) <= 0x20; };
    while (i < value.size()) {
        while (i < value.size() && delimiter(value[i])) ++i;
        const auto start = i;
        while (i < value.size() && !delimiter(value[i])) ++i;
        if (i > start) event.samples.push_back(value.substr(start, i - start));
    }
    return event;
}
std::optional<Theme> find_theme(const rules::IniFile& ini, std::string_view name) {
    if (!ini.find_section(name)) return std::nullopt;
    // 0x00720480 strips leading '$' and '#' from Sound.
    auto sound = ini.read_string(name, "Sound");
    const auto start = sound.find_first_not_of("$#");
    sound = start == std::string::npos ? "" : sound.substr(start);
    return Theme{sound, ini.read_integer(name, "Scenario", 0),
                 ini.read_bool(name, "Normal", true),
                 ini.read_bool(name, "Repeat", false)};
}
}
