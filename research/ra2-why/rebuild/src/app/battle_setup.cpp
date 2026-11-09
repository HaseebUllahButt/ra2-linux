#include "battle_setup.h"

namespace ra2::app {
OpponentChoice first_opponent_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color) {
    OpponentChoice result;
    result.country=rules.read_string(player_country,"Side")=="GDI" ? "Russians" : "Americans";
    result.color=(player_color+1)%8;
    const ui::OpponentSlot* slot=nullptr;
    for (const auto& candidate:setup.opponents)
        if (candidate.opponent && *candidate.opponent!=ui::Opponent::Closed) { slot=&candidate; break; }
    if (!slot) return result;
    const auto* registry=rules.find_section("Countries");
    if (registry && slot->country>=0 && static_cast<std::size_t>(slot->country)<registry->entries.size()) {
        const auto& id=registry->entries[static_cast<std::size_t>(slot->country)].value;
        const auto side=rules.read_string(id,"Side");
        if (rules.find_section(id) && (side=="GDI" || side=="Nod") && rules.read_bool(id,"Multiplay",false))
            result.country=id;
    }
    if (slot->color>=0 && slot->color<8 && slot->color!=player_color) result.color=slot->color;
    return result;
}

bool short_game_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules) {
    return setup.short_game.value_or(rules.read_bool("MultiplayerDialogSettings","ShortGame",true));
}

bool super_weapons_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules) {
    return setup.super_weapons.value_or(rules.read_bool("MultiplayerDialogSettings","SuperWeaponsAllowed",true));
}
}
