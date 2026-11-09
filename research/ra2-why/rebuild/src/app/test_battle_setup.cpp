#include "battle_setup.h"
#include <cassert>
#include <string_view>

int main() {
    using namespace ra2;
    rules::IniFile rules;
    const std::string_view text=
        "[Countries]\n0=Americans\n1=Alliance\n2=Germans\n3=Russians\n4=Confederation\n5=Neutral\n"
        "[Americans]\nSide=GDI\nMultiplay=yes\n[Alliance]\nSide=GDI\nMultiplay=yes\n"
        "[Germans]\nSide=GDI\nMultiplay=yes\n[Russians]\nSide=Nod\nMultiplay=yes\n"
        "[Confederation]\nSide=Nod\nMultiplay=yes\n[Neutral]\nSide=Civilian\nMultiplay=no\n"
        "[MultiplayerDialogSettings]\nShortGame=yes\n";
    rules.parse(text);
    ui::SkirmishSetup setup;
    // No open slot: the old rule stays, so saved test hashes do not change.
    auto choice=app::first_opponent_choice(setup,rules,"Americans",0);
    assert(choice.country=="Russians" && choice.color==1);
    choice=app::first_opponent_choice(setup,rules,"Russians",7);
    assert(choice.country=="Americans" && choice.color==0);
    // A closed first slot is skipped. The second slot gives country and color.
    setup.opponents[0].opponent=ui::Opponent::Closed;
    setup.opponents[0].country=3;
    setup.opponents[1].opponent=ui::Opponent::Medium;
    setup.opponents[1].country=2;
    setup.opponents[1].color=5;
    choice=app::first_opponent_choice(setup,rules,"Americans",0);
    assert(choice.country=="Germans" && choice.color==5);
    // The same faction as the player is allowed, as in the original.
    setup.opponents[1].country=4;
    choice=app::first_opponent_choice(setup,rules,"Russians",0);
    assert(choice.country=="Confederation");
    // Random country and color keep the old rule.
    setup.opponents[1].country=-2;
    setup.opponents[1].color=-2;
    choice=app::first_opponent_choice(setup,rules,"Americans",3);
    assert(choice.country=="Russians" && choice.color==4);
    // A passive country, a bad index and the player's own color are refused.
    setup.opponents[1].country=5;
    setup.opponents[1].color=3;
    choice=app::first_opponent_choice(setup,rules,"Americans",3);
    assert(choice.country=="Russians" && choice.color==4);
    setup.opponents[1].country=99;
    assert(app::first_opponent_choice(setup,rules,"Americans",3).country=="Russians");
    // Short Game: the saved choice wins; the rules give the default.
    assert(app::short_game_choice(setup,rules));
    setup.short_game=false;
    assert(!app::short_game_choice(setup,rules));
    rules::IniFile empty;
    setup.short_game.reset();
    assert(app::short_game_choice(setup,empty));
    // Super Weapons: on without a saved choice; the saved choice wins.
    assert(app::super_weapons_choice(setup,empty));
    setup.super_weapons=false;
    assert(!app::super_weapons_choice(setup,rules));
}
