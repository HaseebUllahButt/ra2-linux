#pragma once
// Hand-off from the skirmish setup screen to the first playable match.
#include "../rules/ini_file.h"
#include "../ui/ui.h"
#include <cstddef>
#include <string>
#include <vector>

namespace ra2::app {
struct OpponentChoice {
    std::string country;
    int color=0;
    ui::Opponent difficulty=ui::Opponent::Easy;
};
// Country and color of the first open opponent slot. The slot country is an
// index into [Countries], as the setup screen stores it. A random (-2),
// unknown or non-multiplayer country keeps the old rule: the other base
// faction. A random or taken color keeps the old rule: the next color.
// TODO-ORIGINAL: the original draws random countries and colors from the
// scenario random stream, and uses every open slot.
OpponentChoice first_opponent_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color);
// One entry for each open opponent slot, in slot order, at most max_opponents
// (the free start positions of the map) and at most 7. Each entry follows the
// rules above; a color already in use moves to the next free color. With no
// open slot the result is one Easy opponent, as before.
std::vector<OpponentChoice> opponent_choices(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color, std::size_t max_opponents);
// The Short Game option. Without a saved choice the original shows the
// [MultiplayerDialogSettings] default.
bool short_game_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules);
// The Super Weapons option (00A3D2C3), with the same default rule.
bool super_weapons_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules);
}
