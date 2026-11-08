#include "ui.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace ra2::ui {
namespace {
std::optional<int> integer(const rules::IniFile& ini, const char* section, const char* key) {
    const auto* entry = ini.find(section, key);
    return entry ? rules::IniFile::integer_value(entry->value) : std::nullopt;
}
std::optional<bool> boolean(const rules::IniFile& ini, const char* key) {
    const auto* entry = ini.find("Skirmish", key);
    return entry ? rules::IniFile::bool_value(entry->value) : std::nullopt;
}
std::optional<Opponent> opponent(int value) {
    switch (value) {
    case 1: return Opponent::Closed;
    case 4: return Opponent::Easy;
    case 5: return Opponent::Medium;
    case 6: return Opponent::Hard;
    default: return std::nullopt;
    }
}
} // namespace

std::optional<int> credit_from_slider_pointer(int local_x, int width,
                                              int minimum, int maximum, int increment) noexcept {
    // 0x0061DA52 reserves 50 pixels for the value and 13 for the grip.
    // 0x0061E545 clamps the pointer and quantizes the absolute value.
    if (width<=63 || width>65535 || minimum>maximum || increment<=0) return std::nullopt;
    const std::int64_t travel=width-63;
    const std::int64_t range=std::int64_t(maximum)-minimum;
    const auto pointer=std::clamp<std::int64_t>(std::int64_t(local_x)-5,1,travel+1);
    const auto offset=std::min(range,(pointer-1)*(range+1)/travel);
    const auto value=(std::int64_t(minimum)+offset)/increment*increment;
    if (value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(value);
}

std::vector<SkirmishMap> load_skirmish_maps(const fs::MixFileSystem& files,
                                             const ra2text::CsfFile& strings) {
    // 0x006999D9 reads the MultiMaps section of the edition's original PKT.
    const auto packet = files.read_file(files.variant() == fs::GameVariant::RedAlert2 ?
                                        "MISSIONS.PKT" : "MISSIONSMD.PKT");
    if (!packet) throw std::runtime_error("Missing original multiplayer map catalog");
    std::vector<SkirmishMap> result;
    auto append = [&](const std::vector<std::uint8_t>& bytes, const std::string& source,
                      const std::shared_ptr<fs::MixArchive>& archive, bool required) {
      rules::IniFile ini;
      ini.parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      const auto* maps = ini.find_section("MultiMaps");
      if (!maps) {
          if (required) throw std::runtime_error("Missing MultiMaps section");
          return;
      }
      for (const auto& entry : maps->entries) {
        // 0x0069A3B0 appends .MAP and reads each matching catalog section.
        const auto id = rules::IniFile::trim(entry.value);
        if (id.empty() || id.size()>128 || id.find_first_of("/\\:")!=std::string_view::npos ||
            id.find("..")!=std::string_view::npos)
            throw std::runtime_error("Invalid multiplayer map name");
        SkirmishMap map;
        map.id = id;
        map.filename = map.id + ".MAP";
        map.source_archive = source;
        const auto* description_text = ini.find(map.id, "DescriptionText");
        if (description_text) map.name = description_text->value;
        else if (const auto* description = ini.find(map.id, "Description")) {
            map.description_key = description->value;
            if (const auto* text = strings.find(description->value)) map.name = text->text;
        }
        map.min_players = ini.read_integer(map.id, "MinPlayers", 2);
        map.max_players = ini.read_integer(map.id, "MaxPlayers", 4);
        map.modes = ini.read_list(map.id, "GameMode");
        map.available = (archive && archive->find(map.filename)) || bool(files.locate(map.filename)) ||
                        bool(files.read_file(map.filename));
        result.push_back(std::move(map));
      }
    };
    append(*packet, "", nullptr, true);
    // 0x00699980 appends loose PKT and MMX/YRO packet entries. Windows does
    // not guarantee directory enumeration order; sort for reproducible Linux.
    std::vector<std::filesystem::path> loose, archives;
    const bool base = files.variant() == fs::GameVariant::RedAlert2;
    for (const auto& file : std::filesystem::directory_iterator(files.directory())) {
        if (!file.is_regular_file()) continue;
        auto ext=file.path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
        if (ext==".PKT") loose.push_back(file.path());
        else if (ext==(base ? ".MMX" : ".YRO")) archives.push_back(file.path());
    }
    std::sort(loose.begin(),loose.end());
    std::sort(archives.begin(),archives.end());
    for (const auto& path : loose) {
        auto name=path.filename().string();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
        if (name==(base ? "MISSIONS.PKT" : "MISSIONSMD.PKT")) continue;
        if (const auto bytes=files.read_file(path.filename().string())) append(*bytes,"",nullptr,false);
    }
    for (const auto& path : archives) {
        const auto archive=fs::MixArchive::open(path);
        const auto* item=archive->find(path.stem().string()+".PKT");
        if (item) append(archive->read(*item),path.filename().string(),archive,false);
    }
    return result;
}

SkirmishSetup read_skirmish_setup(const rules::IniFile& preferences,
                                  const rules::IniFile* base_rules,
                                  const rules::IniFile* expansion_rules) {
    SkirmishSetup setup;
    // 0x00665650 initializes RulesClass. 0x00671EA0 overlays the original
    // MultiplayerDialogSettings before 0x00697F10 reads saved preferences.
    auto rule_integer = [&](const char* key, int value) {
        if (base_rules) value = base_rules->read_integer("MultiplayerDialogSettings",key,value);
        if (expansion_rules) value = expansion_rules->read_integer("MultiplayerDialogSettings",key,value);
        return value;
    };
    auto rule_bool = [&](const char* key, bool value) {
        if (base_rules) value = base_rules->read_bool("MultiplayerDialogSettings",key,value);
        if (expansion_rules) value = expansion_rules->read_bool("MultiplayerDialogSettings",key,value);
        return value;
    };
    setup.game_mode = integer(preferences, "Skirmish", "GameMode");
    setup.scenario_index = integer(preferences, "Skirmish", "ScenIndex").value_or(0);
    setup.min_credits = rule_integer("MinMoney",2500);
    setup.max_credits = rule_integer("MaxMoney",10000);
    setup.credit_increment = rule_integer("MoneyIncrement",100);
    setup.game_speed = integer(preferences, "Skirmish", "GameSpeed").value_or(rule_integer("GameSpeed",6));
    setup.credits = integer(preferences, "Skirmish", "Credits").value_or(rule_integer("Money",10000));
    setup.unit_count = integer(preferences, "Skirmish", "UnitCount").value_or(rule_integer("UnitCount",10));
    setup.short_game = boolean(preferences, "ShortGame").value_or(rule_bool("ShortGame",false));
    setup.super_weapons = boolean(preferences, "SuperWeaponsAllowed").value_or(rule_bool("SuperWeaponsAllowed",true));
    setup.build_off_ally = boolean(preferences, "BuildOffAlly").value_or(rule_bool("BuildOffAlly",true));
    setup.mcv_repacks = boolean(preferences, "MCVRepacks").value_or(rule_bool("MCVRedeploys",true));
    setup.crates = boolean(preferences, "CratesAppear").value_or(rule_bool("Crates",true));
    // 0x006980C0 reads the player's saved country and color separately.
    if (const auto* country = preferences.find("MultiPlayer", "Side")) setup.country = country->value;
    setup.color = integer(preferences, "MultiPlayer", "Color");
    for (std::size_t i=0; i<setup.opponents.size(); ++i) {
        auto& slot = setup.opponents[i];
        // 0x006980C0 passes 6 for the first slot and 1 for the others.
        slot.opponent = i == 0 ? Opponent::Hard : Opponent::Closed;
        char name[7]; std::snprintf(name, sizeof name, "Slot%02u", static_cast<unsigned>(i+1));
        const auto* entry = preferences.find("Skirmish", name);
        if (!entry) continue;
        const auto fields = rules::IniFile::split_list(entry->value);
        if (!fields.empty()) {
            const auto value = rules::IniFile::integer_value(fields[0]);
            slot.opponent = value ? opponent(*value) : std::nullopt;
        }
        if (fields.size()>1) {
            const auto value = rules::IniFile::integer_value(fields[1]);
            if (value) slot.country = *value;
        }
        if (fields.size()>2) {
            const auto value = rules::IniFile::integer_value(fields[2]);
            if (value) slot.color = *value;
        }
    }
    return setup;
}
} // namespace ra2::ui
