#include "battle_setup.h"

namespace ra2::app {
namespace {
OpponentChoice slot_choice(const ui::OpponentSlot* slot, const rules::IniFile& rules,
    const std::string& player_country, int player_color) {
    OpponentChoice result;
    result.country=rules.read_string(player_country,"Side")=="GDI" ? "Russians" : "Americans";
    result.color=(player_color+1)%8;
    if (!slot) return result;
    if (slot->opponent) result.difficulty=*slot->opponent;
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
}

OpponentChoice first_opponent_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color) {
    for (const auto& candidate:setup.opponents)
        if (candidate.opponent && *candidate.opponent!=ui::Opponent::Closed)
            return slot_choice(&candidate,rules,player_country,player_color);
    return slot_choice(nullptr,rules,player_country,player_color);
}

std::vector<OpponentChoice> opponent_choices(const ui::SkirmishSetup& setup, const rules::IniFile& rules,
    const std::string& player_country, int player_color, std::size_t max_opponents) {
    std::vector<OpponentChoice> result;
    bool used[8]={};
    if (player_color>=0 && player_color<8) used[player_color]=true;
    for (const auto& candidate:setup.opponents) {
        if (result.size()>=max_opponents || result.size()>=7) break;
        if (!candidate.opponent || *candidate.opponent==ui::Opponent::Closed) continue;
        auto choice=slot_choice(&candidate,rules,player_country,player_color);
        // 0x004E4C20 reserves each color for one player. Eight colors serve eight players.
        for (int step=0;step<8 && used[choice.color];++step) choice.color=(choice.color+1)%8;
        used[choice.color]=true;
        result.push_back(std::move(choice));
    }
    if (result.empty() && max_opponents>0) result.push_back(slot_choice(nullptr,rules,player_country,player_color));
    return result;
}

bool short_game_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules) {
    return setup.short_game.value_or(rules.read_bool("MultiplayerDialogSettings","ShortGame",true));
}

bool super_weapons_choice(const ui::SkirmishSetup& setup, const rules::IniFile& rules) {
    return setup.super_weapons.value_or(rules.read_bool("MultiplayerDialogSettings","SuperWeaponsAllowed",true));
}
}
