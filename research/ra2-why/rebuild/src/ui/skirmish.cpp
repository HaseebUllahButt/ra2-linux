#include "ui.h"
#include "../rules/ini_file.h"
#include <cstdio>

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

SkirmishSetup read_skirmish_setup(const rules::IniFile& preferences) {
    SkirmishSetup setup;
    // 0x00697F10 reads the Skirmish section. Missing values other than
    // ScenIndex depend on live game rules and are left unset here.
    setup.game_mode = integer(preferences, "Skirmish", "GameMode");
    setup.scenario_index = integer(preferences, "Skirmish", "ScenIndex").value_or(0);
    setup.game_speed = integer(preferences, "Skirmish", "GameSpeed");
    setup.credits = integer(preferences, "Skirmish", "Credits");
    setup.unit_count = integer(preferences, "Skirmish", "UnitCount");
    setup.short_game = boolean(preferences, "ShortGame");
    setup.super_weapons = boolean(preferences, "SuperWeaponsAllowed");
    setup.build_off_ally = boolean(preferences, "BuildOffAlly");
    setup.mcv_repacks = boolean(preferences, "MCVRepacks");
    setup.crates = boolean(preferences, "CratesAppear");
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
