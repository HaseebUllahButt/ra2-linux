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
    {
        // Every open slot becomes one opponent, in slot order.
        ui::SkirmishSetup many;
        auto all=app::opponent_choices(many,rules,"Americans",0,7);
        assert(all.size()==1 && all[0].country=="Russians" && all[0].color==1 &&
               all[0].difficulty==ui::Opponent::Easy);
        many.opponents[0].opponent=ui::Opponent::Hard; many.opponents[0].country=2; many.opponents[0].color=5;
        many.opponents[1].opponent=ui::Opponent::Closed; many.opponents[1].country=3;
        many.opponents[2].opponent=ui::Opponent::Medium; many.opponents[2].country=-2; many.opponents[2].color=5;
        many.opponents[4].opponent=ui::Opponent::Easy; many.opponents[4].country=4; many.opponents[4].color=0;
        all=app::opponent_choices(many,rules,"Americans",0,7);
        assert(all.size()==3);
        assert(all[0].country=="Germans" && all[0].color==5 && all[0].difficulty==ui::Opponent::Hard);
        // Random country: the other faction. A used color moves to the next free one.
        assert(all[1].country=="Russians" && all[1].color==6 && all[1].difficulty==ui::Opponent::Medium);
        // The player's own color is refused, then the default 1 is free.
        assert(all[2].country=="Confederation" && all[2].color==1 && all[2].difficulty==ui::Opponent::Easy);
        // The map start positions limit the number of opponents.
        all=app::opponent_choices(many,rules,"Americans",0,2);
        assert(all.size()==2 && all[1].country=="Russians");
        assert(app::opponent_choices(many,rules,"Americans",0,0).empty());
        // Seven opponents and the player get eight different colors.
        for (auto& slot:many.opponents) { slot.opponent=ui::Opponent::Easy; slot.country=-2; slot.color=-2; }
        all=app::opponent_choices(many,rules,"Americans",3,7);
        assert(all.size()==7);
        bool seen[8]={}; seen[3]=true;
        for (const auto& one:all) { assert(!seen[one.color]); seen[one.color]=true; }
        // The first entry equals the single-opponent rule.
        assert(all[0].country==app::first_opponent_choice(many,rules,"Americans",3).country);
    }
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
