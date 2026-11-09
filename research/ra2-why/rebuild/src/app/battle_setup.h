#pragma once
// Hand-off from the skirmish setup screen to the first playable match.
#include "../rules/ini_file.h"
#include "../ui/ui.h"
#include <string>

namespace ra2::app {
struct OpponentChoice {
    std::string country;
    int color=0;
};
// Country and color of the first open opponent slot. The slot country is an
// index into [Countries], as the setup screen stores it. A random (-2),
// unknown or non-multiplayer country keeps the old rule: the other base
// faction. A random or taken color keeps the old rule: the next color.
// TODO-ORIGINAL: the original draws random countries and colors from the
// scenario random stream, and uses every open slot.
OpponentChoice first_opponent_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color);
// The Short Game option. Without a saved choice the original shows the
// [MultiplayerDialogSettings] default.
bool short_game_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules);
// The Super Weapons option (00A3D2C3), with the same default rule.
bool super_weapons_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules);
}
