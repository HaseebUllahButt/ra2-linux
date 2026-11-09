#include "battle_score.h"
#include <cassert>
#include <iostream>
using namespace ra2;
int main() {
    app::BattleScore score({{101,"Player"},{202,"Computer"}});
    score.clock(0,false);score.clock(10000,true);score.clock(40000,false);
    score.clock(100000,true);score.clock(130000,false);
    score.harvest(101,1000,1500);score.harvest(101,1500,1400);score.harvest(202,1000,1700);
    sim::PrototypeCombatEvent kill;
    kill.kind=sim::PrototypeCombatEventKind::Death;kill.source_house=101;kill.target_house=202;kill.target_id=10;
    score.combat({kill});score.combat({kill});
    kill.source_house=202;kill.target_house=101;kill.target_id=11;score.combat({kill});
    auto out=score.finish(true);
    assert(out.seconds==60 && ui::format_score_time(out.seconds)=="example.invalid");
    assert(out.houses[0].credits_harvested==500 && out.houses[1].credits_harvested==700);
    assert(out.houses[0].kills()==1 && out.houses[0].losses()==1);
    assert(out.houses[0].kills_of[0]==0 && out.houses[0].kills_of[1]==1);
    assert(out.houses[1].kills_of[0]==1 && out.houses[1].kills_of[1]==0);
    kill.kind=sim::PrototypeCombatEventKind::Damage;kill.target_id=12;score.combat({kill});
    assert(score.finish(true).houses[0].losses()==1);
    score.clock(200000,false);assert(score.finish(true).seconds==60);
    std::cout<<"Match score: play time, pause, harvest, attribution and duplicate deaths passed\n";
}
