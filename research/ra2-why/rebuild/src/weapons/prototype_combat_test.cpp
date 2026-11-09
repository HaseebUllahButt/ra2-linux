#include <fs/mix_bootstrap.h>
#include <rules/ini_file.h>
#include <sim/damage_runtime.h>
#include <app/battle_fallout.h>
#include <app/battle_score.h>
#include <app/battle_effects.h>
#include <weapons/detonation.h>
#include <weapons/fire_timer.h>
#include <array>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <string_view>

using namespace ra2;
namespace world=ra2::session::world;
namespace {
void check(bool value,const char* why) {if (!value) throw std::runtime_error(why);}
rules::IniFile load(fs::MixFileSystem& files,const char* name) {
    const auto bytes=files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("missing ")+name);
    rules::IniFile ini;ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));return ini;
}
world::Snapshot make_world(const std::string& source_type,const std::string& target_type,
                           world::Kind source_kind,world::Kind target_kind,int source_hp,int target_hp) {
    world::Snapshot w;w.random=sim::Random(0).state();
    for (int y=46;y<=54;++y) for (int x=46;x<=56;++x) {
        world::CellState c;c.allocated=true;c.heads.emplace();c.cell_flag_100=false;
        c.ground_occupancy_124=0;c.terrain.emplace();c.terrain->cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        c.terrain->floor_world_height=0;c.terrain->ramp_index=0;c.terrain->overlay_type_index=-1;
        w.cells.emplace(y*512+x,c);
    }
    world::House a;a.id=101;a.reload_multiplier=1;a.firepower_multiplier=1;
    // The target house is human and its object is on mission 13, so the
    // 006CDC40 retaliation stays off (PlayerReturnFire=no) in one-sided tests.
    world::House b=a;b.id=102;b.human_controlled=true;w.houses={a,b};
    world::Entity source;source.id=1;source.house_id=101;source.type=source_type;source.kind=source_kind;
    source.lifetime=world::Lifetime::Placed;source.position=sim::Coord{50*256+128,50*256+128,0};
    source.cell=ra2map::Cell{50,50};source.next=0;source.resolved_hp=source.maximum_hp=source_hp;
    source.active=true;source.in_limbo=false;source.hull_facing_raw=0;source.turret_facing_raw=0;
    source.weapon_timer_start_frame=-1;source.weapon_timer_duration=0;source.burst_index=0;
    source.armor_multiplier=1;source.house_armor_multiplier=1;source.firepower_multiplier=1;
    source.collision_eligible=true;source.collision_virtual_position=source.position;
    auto target=source;target.id=2;target.house_id=102;target.type=target_type;target.kind=target_kind;
    target.position=sim::Coord{54*256+128,50*256+128,0};target.cell=ra2map::Cell{54,50};
    target.resolved_hp=target.maximum_hp=target_hp;target.collision_virtual_position=target.position;target.next=3;
    target.mission=13;
    world::Entity tree;tree.id=3;tree.type="TREE";tree.kind=world::Kind::Terrain;tree.lifetime=world::Lifetime::Placed;
    tree.position=*target.position;tree.next=0;tree.collision_eligible=false;
    w.cells.at(50*512+50).heads->ground_head=1;
    w.cells.at(50*512+54).heads->ground_head=2;w.cells.at(50*512+54).ground_occupancy_124=0x20;
    w.entities={source,target,tree};return w;
}
void same_rng(const sim::RandomState& a,const sim::RandomState& b) {
    check(a.first==b.first && a.second==b.second && a.table==b.table,"replay RNG differs");
}
void air_ground_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto w=make_world("GAPILL","ZEP",world::Kind::Structure,world::Kind::Unit,400,2000);
    w.entities[0].mission=5;
    w.entities[1].position->z=750;
    sim::PrototypeCombatRuntime pill(rules,map,art,exe);
    check(!pill.can_attack(w,1,2) && !pill.attack(w,1,2),"Pillbox accepted an airborne Kirov");
    const auto rng=w.random;
    for(unsigned frame=0;frame<80;++frame)check(pill.tick(w,frame).fired==0,"Pillbox auto-fired at air");
    same_rng(rng,w.random);
    w.entities[1].position->z=0;
    check(!pill.can_attack(w,1,2),"Pillbox accepted a Kirov with unresolved flight height");
    w.entities[1].type="HTNK";
    // The original threshold is inclusive. Ground height is relative to terrain.
    w.entities[1].position->z=207;
    check(pill.can_attack(w,1,2),"low object was treated as airborne");
    w.entities[1].position->z=208;
    check(!pill.can_attack(w,1,2),"air threshold was off by one");
    w.entities[1].position->z=416;
    w.cells.at(50*512+54).terrain->floor_world_height=416;
    check(pill.can_attack(w,1,2),"ground object on a hill was treated as air");
    w.cells.at(50*512+54).terrain->floor_world_height=0;
    w.entities[0].type="NAFLAK";w.entities[0].mission=13;
    check(pill.can_attack(w,1,2),"Flak Cannon rejected air");
    w.entities[1].type="ZEP";w.entities[1].position->z=0;
    check(pill.can_attack(w,1,2),"Flak Cannon rejected the Kirov flight class");
    w.entities[1].type="HTNK";
    w.entities[1].position->z=0;
    check(!pill.can_attack(w,1,2),"AA-only Flak Cannon accepted ground");
    w.entities[1].position->z=750;
    check(pill.attack(w,1,2),"Flak Cannon air order rejected");
    const auto flak=pill.tick(w,80);
    if(!flak.unresolved.empty())throw std::runtime_error(flak.unresolved.front());
    check(flak.fired>0,"Flak Cannon did not fire at air");
    pill.stop(w,1);w.entities[0].weapon_timer_start_frame=-1;w.entities[0].weapon_timer_duration=0;
    w.entities[1].position->z=0;
    w.entities[0].type="FLAKT";w.entities[0].kind=world::Kind::Infantry;
    w.entities[1].type="HTNK";w.entities[1].resolved_hp=w.entities[1].maximum_hp=400;
    check(pill.attack(w,1,2),"Flak Trooper did not choose its ground weapon");
    bool fired=false;
    for(unsigned frame=100;frame<130 && !fired;++frame)for(const auto& e:pill.tick(w,frame).events)
        if(e.kind==sim::PrototypeCombatEventKind::Shot && e.source_id==1) {
            check(e.weapon==rules.read_string("FLAKT","Primary"),"wrong Flak Trooper ground weapon");
            fired=true;
        }
    check(fired,"Flak Trooper ground weapon stayed silent");
    pill.stop(w,1);w.entities[0].weapon_timer_start_frame=-1;w.entities[0].weapon_timer_duration=0;
    w.entities[1].position->z=750;
    check(pill.attack(w,1,2),"Flak Trooper did not choose its air weapon");
    bool air_fired=false;
    for(unsigned frame=150;frame<180 && !air_fired;++frame)for(const auto& e:pill.tick(w,frame).events)
        if(e.kind==sim::PrototypeCombatEventKind::Shot && e.source_id==1) {
            check(e.weapon==rules.read_string("FLAKT","Secondary"),"wrong Flak Trooper air weapon");
            air_fired=true;
        }
    check(air_fired,"Flak Trooper air weapon stayed silent");
    w.entities[1].position->z=0;
    // An existing order must be rejected when its target takes off.
    pill.stop(w,1);w.entities[0].type="GAPILL";w.entities[0].kind=world::Kind::Structure;
    w.entities[0].weapon_timer_start_frame=-1;w.entities[0].weapon_timer_duration=0;w.entities[0].mission=13;
    check(pill.attack(w,1,2),"ground order rejected before takeoff");
    w.entities[1].position->z=750;
    const auto took_off=pill.tick(w,181);
    check(took_off.fired==0 && took_off.unresolved.empty() && w.entities[0].target_id==0,
          "Pillbox kept firing after takeoff");
    std::cout<<"Projectile AA/AG, height threshold, hills, secondary weapons and takeoff passed\n";
}
void fallen_house_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe,fs::MixFileSystem& files) {
    auto w=make_world("HTNK","GACNST",world::Kind::Unit,world::Kind::Structure,400,1);
    w.setup_progress=world::SetupProgress::PrototypeComplete;
    auto third=w.houses[0];third.id=103;w.houses.push_back(third);
    const auto add=[&](unsigned id,unsigned owner,world::Kind kind,const char* type,int x,int y) {
        auto e=w.entities[0];e.id=id;e.house_id=owner;e.kind=kind;e.type=type;e.next=0;
        e.resolved_hp=e.maximum_hp=1000;e.target_id=0;
        e.cell=ra2map::Cell{std::uint16_t(x),std::uint16_t(y)};
        e.position=sim::Coord{x*256+128,y*256+128,0};
        auto& c=w.cells.at(y*512+x);c.heads->ground_head=id;c.ground_occupancy_124=0x20;
        w.entities.push_back(e);
    };
    add(4,103,world::Kind::Structure,"GACNST",55,52);
    add(5,102,world::Kind::Unit,"HTNK",54,51);
    add(6,102,world::Kind::Infantry,"E1",53,52);
    add(8,101,world::Kind::Structure,"GACNST",46,46);
    auto product=w.entities[0];product.id=7;product.house_id=102;
    product.lifetime=world::Lifetime::Created;product.cell.reset();product.position.reset();
    w.entities.push_back(product);
    sim::PrototypeCombatRuntime combat(rules,{},art,exe);
    sim::VictoryState victory;sim::VictoryRules settings;settings.base_units={"AMCV","SMCV"};
    app::BattleScore score({{101,"Player"},{102,"Computer"},{103,"Computer 2"}});
    check(combat.attack(w,1,2)&&combat.attack(w,5,4),"three-house orders failed");
    bool saw_fall=false;
    for(unsigned frame=0;frame<200;++frame) {
        const auto fight=combat.tick(w,frame);score.combat(fight.events);
        check(fight.unresolved.empty(),"three-house combat needs input");
        const auto cleanup=app::resolve_fallen_houses(w,victory,combat,101,{102,103},settings,frame);
        score.combat(cleanup.events);
        check(cleanup.unresolved.empty(),"fallen-house cleanup needs input");
        if(w.entities[1].lifetime!=world::Lifetime::Destroyed) {
            check(cleanup.destroyed==0,"house fell before its last building died");continue;
        }
        saw_fall=true;
        check(victory.fallen(102)&&!victory.settled(),"one lost house ended the three-house match");
        check(cleanup.destroyed==3 && cleanup.events.size()==3,"remaining army or factory product survived");
        for(const auto& event:cleanup.events)check(event.kind==sim::PrototypeCombatEventKind::Death &&
            event.frame==frame && event.source_id==0 && event.source_house==0 && event.target_house==102,
            "cleanup death has wrong frame or attacker");
        for(const auto& e:w.entities)if(e.house_id==102)
            check(e.lifetime==world::Lifetime::Destroyed && e.resolved_hp==0 && e.target_id==0,
                "fallen house kept a live actor or target");
        check(w.cells.at(51*512+54).heads->ground_head==0 && w.cells.at(51*512+54).ground_occupancy_124==0 &&
            w.cells.at(52*512+53).heads->ground_head==0 && w.cells.at(52*512+53).ground_occupancy_124==0,
            "fallen army kept occupied cells");
        check(w.entities[2].lifetime==world::Lifetime::Placed && w.cells.at(50*512+54).heads->ground_head==3,
            "cleanup removed another house or changed a surviving chain");
        const auto tally=score.finish(false);
        check(tally.houses[0].kills()==1 && tally.houses[1].losses()==4 && tally.houses[2].losses()==0,
            "mass death changed kill credit or lost its losses");
        app::BattleEffects effects(files);
        const auto project=[](sim::Coord at)->std::optional<app::EffectPoint> {
            if(at.x<0||at.y<0)return {};
            return app::EffectPoint{at.x/32,at.y/32};
        };
        effects.observe(w,{},frame,project,&cleanup.events,&rules);
        check(effects.active()==2,"placed army deaths did not make effects or a limbo product made one");
        effects.observe(w,{},frame,project,&cleanup.events,&rules);
        check(effects.active()==2,"same-frame deaths made duplicate effects");
        check(combat.destroy_house(w,102,frame).events.empty(),"house destruction repeated deaths");
        const auto later=combat.tick(w,frame+1);
        for(const auto& event:later.events)if(event.kind==sim::PrototypeCombatEventKind::Shot)
            check(event.source_house!=102,"fallen tank kept firing");
        break;
    }
    check(saw_fall,"last building never died");
    std::cout<<"Three-house defeat removes its army in the same frame, with events, score and clear cells\n";
}
void tank_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto a=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400),b=a;
    // One object can be visible from more than one Cell through the same link.
    a.cells.at(51*512+54).heads->ground_head=2;
    b.cells.at(51*512+54).heads->ground_head=2;
    sim::PrototypeCombatRuntime one(rules,map,art,exe),two(rules,map,art,exe);
    check(one.attack(a,1,2) && two.attack(b,1,2),"tank attack order rejected");
    std::size_t fired=0,impacts=0,deaths=0;bool saw_projectile=false,saw_damage=false;
    sim::Random oracle(0);
    check(weapons::base_single_shot_unit_delay({65,1,false,false,false,false,1},oracle)==67,"reload oracle");
    for (std::uint64_t frame=0;frame<1000 && !deaths;++frame) {
        const auto r=one.tick(a,frame),s=two.tick(b,frame);
        if (!r.unresolved.empty()) throw std::runtime_error(r.unresolved.front());
        if (!s.unresolved.empty()) throw std::runtime_error(s.unresolved.front());
        check(r.fired==s.fired && r.impacts==s.impacts && r.destroyed==s.destroyed,"replay actions differ");
        check(a.entities[1].resolved_hp==b.entities[1].resolved_hp &&
              a.entities[0].weapon_timer_start_frame==b.entities[0].weapon_timer_start_frame &&
              a.entities[0].weapon_timer_duration==b.entities[0].weapon_timer_duration,"replay state differs");
        same_rng(a.random,b.random);
        if (frame<11) check(r.fired==0 && !a.random.first,"tank fired before its turret turned");
        if (frame==11) {
            check(r.fired==1 && a.entities[0].weapon_timer_start_frame==11 &&
                  a.entities[0].weapon_timer_duration==67,"real 120mm reload");
            same_rng(a.random,oracle.state());
        }
        if (r.impacts && !impacts) {
            check(a.entities[1].resolved_hp==310,"real AP damage against heavy armor");
            check(weapons::base_complete_single_cluster_scatter(true,oracle),"scatter oracle");
            same_rng(a.random,oracle.state());
        }
        saw_projectile|=!one.projectiles().empty();saw_damage|=*a.entities[1].resolved_hp<400;
        fired+=r.fired;impacts+=r.impacts;deaths+=r.destroyed;
    }
    check(saw_projectile && saw_damage && fired>=5 && impacts>=5 && deaths==1,"tank did not fight and die");
    check(a.entities[1].lifetime==world::Lifetime::Destroyed && a.entities[1].resolved_hp==0 &&
          a.entities[1].active==false && a.entities[1].next==0,"dead tank state");
    check(a.cells.at(50*512+54).heads->ground_head==3 &&
          a.cells.at(51*512+54).heads->ground_head==3 && a.entities[2].next==0,
          "death changed surviving links");
    check(!one.attack(a,1,2),"dead target accepted");
}
void cyclic_link_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    const auto hp=rules.read_integer("E1","Strength");
    auto w=make_world("E1","E1",world::Kind::Infantry,world::Kind::Infantry,hp,hp);
    w.entities[1].position=sim::Coord{52*256+128,50*256+128,0};
    w.entities[1].cell=ra2map::Cell{52,50};w.entities[1].resolved_hp=1;
    w.entities[2].next=2;
    w.cells.at(50*512+54).heads->ground_head=0;w.cells.at(50*512+52).heads->ground_head=2;
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    check(combat.attack(w,1,2),"cycle test attack rejected");
    const auto r=combat.tick(w,0);
    check(!r.unresolved.empty() && w.entities[1].resolved_hp==1 &&
          w.cells.at(50*512+52).heads->ground_head==2 && w.entities[2].next==2,
          "cyclic death chain changed");
}
void overlay_and_range_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile mode,map;
    mode.parse("[120mm]\nDamage=70\n");map.parse("[120mm]\nDamage=60\n");
    auto w=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400);
    sim::PrototypeCombatRuntime combat(rules,mode,map,art,exe);
    w.entities[1].position=sim::Coord{56*256+128,50*256+128,0};
    check(combat.attack(w,1,2),"range test attack rejected");
    const auto before=w.random;const auto out=combat.tick(w,0);
    check(out.out_of_range.size()==1 && out.out_of_range[0]==std::make_pair(1u,2u) &&
          out.fired==0,"out-of-range target did not request an approach");
    same_rng(before,w.random);combat.stop(w,1);check(w.entities[0].target_id==0,"stop kept target");
    w.entities[1].position=sim::Coord{54*256+128,50*256+128,0};
    check(combat.attack(w,1,2),"overlay test attack rejected");
    for (std::uint64_t frame=1;frame<100;++frame) {
        auto result=combat.tick(w,frame);
        if (!result.unresolved.empty()) throw std::runtime_error(result.unresolved.front());
        if (result.impacts) break;
    }
    check(w.entities[1].resolved_hp==340,"map rules did not override mode and base rules");
    rules::IniFile duplicate,empty;
    duplicate.parse("[120mm]\nROF=65\n[120mm]\nDamage=10\n");
    auto other=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400);
    sim::PrototypeCombatRuntime first_section(rules,duplicate,empty,art,exe);
    check(first_section.attack(other,1,2),"duplicate-section test attack rejected");
    for (std::uint64_t frame=0;frame<100;++frame) {
        const auto result=first_section.tick(other,frame);
        if (!result.unresolved.empty()) throw std::runtime_error(result.unresolved.front());
        if (result.impacts) break;
    }
    check(other.entities[1].resolved_hp==310,"duplicate mode section changed original rule lookup");
}
void infantry_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto w=make_world("E1","E1",world::Kind::Infantry,world::Kind::Infantry,
        rules.read_integer("E1","Strength"),rules.read_integer("E1","Strength"));
    w.entities[1].position=sim::Coord{52*256+128,50*256+128,0};w.entities[1].cell=ra2map::Cell{52,50};
    w.entities[1].collision_virtual_position=w.entities[1].position;
    w.cells.at(50*512+54).heads->ground_head=0;w.cells.at(50*512+52).heads->ground_head=2;
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    check(combat.attack(w,1,2),"infantry attack rejected");
    std::size_t shots=0,killed=0;
    for (std::uint64_t frame=0;frame<1000 && !killed;++frame) {
        const auto r=combat.tick(w,frame);
        if (!r.unresolved.empty()) throw std::runtime_error(r.unresolved.front());
        shots+=r.fired;killed+=r.destroyed;
    }
    check(shots>0 && killed==1 && w.entities[1].resolved_hp==0,"infantry did not fight and die");
    check(w.cells.at(50*512+52).heads->ground_head==3,"infantry death link");
}
void yard_volley_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe,
                      const std::string& yard) {
    rules::IniFile map;
    auto w=make_world("HTNK",yard,world::Kind::Unit,world::Kind::Structure,400,
                      rules.read_integer(yard,"Strength"));
    for (unsigned id : {4u,5u}) { auto tank=w.entities.front(); tank.id=id; w.entities.push_back(tank); }
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    for (unsigned id : {1u,4u,5u}) check(combat.attack(w,id,2),"Rhino yard order rejected");
    unsigned first_impact=0,death=0,first_shot=0; std::size_t shots=0;
    for (unsigned frame=0;frame<3000;++frame) {
        const auto result=combat.tick(w,frame);
        check(result.unresolved.empty(),"Rhino yard volley unresolved");
        std::size_t recorded_shots=0,recorded_deaths=0;
        for (const auto& event:result.events) {
            check(event.frame==frame && event.fired_frame<=frame,"event frame is wrong");
            check(event.weapon=="120mm" && event.warhead=="AP" &&
                  event.source_type=="HTNK" && event.target_type==yard &&
                  event.target_id==2 && event.source_house==101 && event.target_house==102,
                  "yard damage attribution is wrong");
            if (event.kind==sim::PrototypeCombatEventKind::Shot) {
                ++recorded_shots;
                check(event.fired_frame==frame && event.hp_before==event.hp_after,"shot changed health");
            } else {
                check(event.fired_frame<frame && event.hp_after<event.hp_before,"impact lost flight timing");
                if (event.kind==sim::PrototypeCombatEventKind::Death) {
                    ++recorded_deaths;check(event.hp_after==0,"death retained health");
                }
            }
        }
        check(recorded_shots==result.fired && recorded_deaths==result.destroyed,
              "shot or death diagnostic is missing");
        if (result.fired && !shots) first_shot=frame;
        shots+=result.fired;
        if (result.impacts && !first_impact) first_impact=frame;
        if (result.destroyed) { death=frame; break; }
    }
    // Base 004B8A30/004B8BE0 with HTNK ROT=5: the turret turns 1280 raw
    // steps per frame from north to east and 00704C00 lets it fire within
    // 2048 raw steps, at frame 11. Before turret turning it fired at frame 0.
    check(first_shot==11 && first_impact==22 && death==417 && shots==21,"yard volley timing or shot count changed");
    std::cout<<"Three HTNK against "<<yard<<" at four cells: first fire="<<first_shot<<" first impact="<<first_impact
             <<" yard death="<<death<<" shots="<<shots<<'\n';
}
void idle_defense_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto w=make_world("NALASR","HTNK",world::Kind::Structure,world::Kind::Unit,400,400);
    w.entities[0].mission=0;
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    w.entities[1].position->x+=8*256;
    check(combat.tick(w,0).fired==0,"Sentry fired beyond range");
    w.entities[1].position->x-=8*256;
    check(combat.tick(w,1).fired==1 && w.entities[0].target_id==2,"idle Sentry did not fire in range");
    check(combat.tick(w,2).fired==0,"idle Sentry bypassed reload");
    // A stale guard target must not block a closer enemy or cause a chase.
    w.entities[1].position->x+=8*256;
    auto close=w.entities[1]; close.id=4; close.position->x-=9*256;
    w.entities.push_back(close);
    const auto replaced=combat.tick(w,3);
    check(replaced.out_of_range.empty() && w.entities[0].target_id==4,
          "distant guard target blocked a nearby enemy");
    w.entities.back().house_id=w.entities[0].house_id;
    check(combat.tick(w,4).out_of_range.empty() && w.entities[0].target_id==0,
          "guard retained an illegal target");
    check(combat.attack(w,1,2),"explicit distant order rejected");
    w.entities.back().house_id=w.entities[1].house_id;
    const auto explicit_order=combat.tick(w,5);
    check(explicit_order.out_of_range.size()==1 && w.entities[0].target_id==2,
          "guard scan replaced an explicit chase target");
    auto tank=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400);
    tank.entities[0].mission=0;
    tank.entities[0].locomotor_moving=true;
    sim::PrototypeCombatRuntime guard(rules,map,art,exe);
    check(guard.tick(tank,0).fired==0,"moving tank acquired a target");
    tank.entities[0].locomotor_moving=false;
    // The turret starts north and the enemy is east: it turns first.
    unsigned defended=0;
    for (unsigned frame=1;frame<40 && !defended;++frame) if (guard.tick(tank,frame).fired) defended=frame;
    check(defended==12,"idle tank did not defend after its turret turned");
}
// A small base: house 101 owns the yard and Rhinos, house 102 owns the
// soldiers. Each object has its own Cell link.
struct Piece { std::uint32_t id,house; const char* type; world::Kind kind; int x,y; };
world::Snapshot make_base(const rules::IniFile& rules,const std::vector<Piece>& pieces,bool human) {
    world::Snapshot w;w.random=sim::Random(0).state();
    for (int y=36;y<=64;++y) for (int x=36;x<=64;++x) {
        world::CellState c;c.allocated=true;c.heads.emplace();c.cell_flag_100=false;
        c.ground_occupancy_124=0;c.terrain.emplace();c.terrain->cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        c.terrain->floor_world_height=0;c.terrain->ramp_index=0;c.terrain->overlay_type_index=-1;
        w.cells.emplace(y*512+x,c);
    }
    world::House a;a.id=101;a.reload_multiplier=1;a.firepower_multiplier=1;a.human_controlled=human;
    world::House b=a;b.id=102;b.human_controlled=false;w.houses={a,b};
    for (const auto& p:pieces) {
        world::Entity e;e.id=p.id;e.house_id=p.house;e.type=p.type;e.kind=p.kind;
        e.lifetime=world::Lifetime::Placed;e.position=sim::Coord{p.x*256+128,p.y*256+128,0};
        e.cell=ra2map::Cell{std::uint16_t(p.x),std::uint16_t(p.y)};e.next=0;
        e.resolved_hp=e.maximum_hp=rules.read_integer(p.type,"Strength");
        e.active=true;e.in_limbo=false;e.hull_facing_raw=0;e.turret_facing_raw=0;
        e.weapon_timer_start_frame=-1;e.weapon_timer_duration=0;e.burst_index=0;
        e.armor_multiplier=1;e.house_armor_multiplier=1;e.firepower_multiplier=1;
        e.collision_eligible=true;e.collision_virtual_position=e.position;
        e.locomotor_moving=false;e.mission=5;
        w.cells.at(p.y*512+p.x).heads->ground_head=p.id;
        w.entities.push_back(e);
    }
    return w;
}
world::Entity& piece(world::Snapshot& w,std::uint32_t id) {
    for (auto& e:w.entities) if (e.id==id) return e;
    throw std::runtime_error("missing piece");
}
// Base 006C6530/006D31E0 idle threat scan and 006CDC40/006D3B60 retaliation.
void yard_defense_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    // Three idle Guard Rhinos 3-4 cells from their yard. Three GIs walk up
    // to the other side of the yard and shoot it from 3 cells.
    const std::vector<Piece> base{{1,101,"NACNST",world::Kind::Structure,50,50},
        {2,101,"HTNK",world::Kind::Unit,53,49},{3,101,"HTNK",world::Kind::Unit,53,51},
        {4,101,"HTNK",world::Kind::Unit,48,52},{5,102,"E1",world::Kind::Infantry,50,47},
        {6,102,"E1",world::Kind::Infantry,51,47},{7,102,"E1",world::Kind::Infantry,49,47}};
    auto w=make_base(rules,base,true);
    piece(w,1).mission=0;
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    for (unsigned id:{5u,6u,7u}) check(combat.attack(w,id,1),"GI yard order rejected");
    const int yard_hp=*piece(w,1).resolved_hp;
    unsigned cleared=0;std::optional<unsigned> first_defense;std::size_t rhino_shots=0;
    for (unsigned frame=0;frame<2000 && !cleared;++frame) {
        const auto r=combat.tick(w,frame);
        check(r.unresolved.empty(),"yard defense unresolved");
        for (const auto& e:r.events) if (e.kind==sim::PrototypeCombatEventKind::Shot && e.source_type=="HTNK") {
            ++rhino_shots;if (!first_defense) first_defense=frame;
            check(e.target_type=="E1","Rhino shot a wrong target");
        }
        if (piece(w,5).lifetime==world::Lifetime::Destroyed && piece(w,6).lifetime==world::Lifetime::Destroyed &&
            piece(w,7).lifetime==world::Lifetime::Destroyed) cleared=frame;
    }
    check(cleared && first_defense && *first_defense<20,"idle Rhinos did not defend their yard");
    check(piece(w,1).lifetime==world::Lifetime::Placed && *piece(w,1).resolved_hp>0 &&
          *piece(w,1).resolved_hp<yard_hp,"yard defense test lost or never hit the yard");
    for (unsigned id:{2u,3u,4u}) check(piece(w,id).target_id.value_or(0)==0,"Rhino kept a dead guard target");
    std::cout<<"Three Guard HTNK defend NACNST from three E1: first defense="<<first_defense.value_or(2000)
             <<" all dead="<<cleared<<" Rhino shots="<<rhino_shots<<" yard hp="<<*piece(w,1).resolved_hp<<'/'<<yard_hp<<'\n';

    // Guard scans the weapon range (5.75 cells). Area Guard scans twice that
    // and approaches the enemy. The GI is 8 cells from the Rhino.
    const std::vector<Piece> far{{1,101,"HTNK",world::Kind::Unit,50,50},{2,102,"E1",world::Kind::Infantry,58,50}};
    auto guard=make_base(rules,far,true);
    sim::PrototypeCombatRuntime g(rules,map,art,exe);
    const auto gr=g.tick(guard,0);
    check(gr.out_of_range.empty() && piece(guard,1).target_id.value_or(0)==0,"Guard scanned beyond weapon range");
    auto area=make_base(rules,far,false);
    piece(area,1).mission=11;
    sim::PrototypeCombatRuntime ag(rules,map,art,exe);
    const auto ar=ag.tick(area,0);
    check(ar.out_of_range.size()==1 && ar.out_of_range[0]==std::make_pair(1u,2u) &&
          piece(area,1).target_id==2u,"Area Guard did not take an enemy in twice weapon range");
    // The Area Guard target stays while the Rhino drives toward it.
    piece(area,1).locomotor_moving=true;
    check(ag.tick(area,1).out_of_range.size()==1 && piece(area,1).target_id==2u,"Area Guard dropped its approach");
    // Base 004C5C1D: a stopped Rhino more than 0.75 * 2944 leptons from its
    // guard place drops the target and asks to drive back.
    piece(area,1).locomotor_moving=false;piece(area,1).position->x+=8*256;
    check(ag.tick(area,2).guard_return.empty() && piece(area,1).target_id==2u,"Area Guard gave up too early");
    piece(area,1).position->x+=256;
    const auto back=ag.tick(area,3);
    check(back.guard_return.size()==1 && back.guard_return[0].first==1 &&
          back.guard_return[0].second.x==50*256+128 && piece(area,1).target_id.value_or(0)!=2u,
          "Area Guard chased beyond its guard range");

    // Retaliation: a Rhino on another mission (13) is shot by a GI.
    // A computer Rhino returns fire. A human Rhino does not, because
    // [CombatDamage] PlayerReturnFire=no and the mission is not Guard.
    const std::vector<Piece> duel{{1,101,"HTNK",world::Kind::Unit,50,50},{2,102,"E1",world::Kind::Infantry,53,50}};
    for (bool human:{false,true}) {
        auto d=make_base(rules,duel,human);
        piece(d,1).mission=13;
        sim::PrototypeCombatRuntime c(rules,map,art,exe);
        check(c.attack(d,2,1),"GI Rhino order rejected");
        bool hit=false;
        for (unsigned frame=0;frame<200 && !hit;++frame) {
            const auto r=c.tick(d,frame);
            for (const auto& e:r.events) hit|=e.kind==sim::PrototypeCombatEventKind::Damage && e.target_id==1;
        }
        check(hit,"GI did not hit the Rhino");
        check(piece(d,1).target_id.value_or(0)==(human?0u:2u),
              human?"human Rhino returned fire outside Guard":"computer Rhino did not return fire");
    }

    // An explicit target dies; the Guard Rhino then takes the next enemy.
    const std::vector<Piece> next{{1,101,"HTNK",world::Kind::Unit,50,50},{2,102,"E1",world::Kind::Infantry,53,50},
        {3,102,"E1",world::Kind::Infantry,50,54}};
    auto n=make_base(rules,next,true);
    piece(n,2).resolved_hp=1;piece(n,1).turret_facing_raw=16384;
    sim::PrototypeCombatRuntime nc(rules,map,art,exe);
    check(nc.attack(n,1,2),"explicit Rhino order rejected");
    unsigned dead=0,retarget=0;
    for (unsigned frame=0;frame<200 && !retarget;++frame) {
        nc.tick(n,frame);
        if (!dead && piece(n,2).lifetime==world::Lifetime::Destroyed) dead=frame;
        if (dead && piece(n,1).target_id==3u) retarget=frame;
    }
    check(dead && retarget && retarget<=dead+1,"Rhino lost Guard after its explicit target died");
}
// Base 004B8A30/004B8BE0 turret turning and 00704C00 fire facing check.
void turret_turn_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto w=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400);
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    check(combat.attack(w,1,2),"turret test attack rejected");
    // The original direction to the east target is 16383 raw steps. ROT=5
    // gives 16383/1280=12 timer frames and a step of 16383/12=1365, so frame 0
    // already shows 3 steps of turn (Current divides before it multiplies).
    const std::array<std::pair<unsigned,std::uint16_t>,4> expected{{{0,3},{6,8193},{10,13653},{11,15018}}};
    std::size_t next=0;
    for (unsigned frame=0;frame<=12;++frame) {
        const auto r=combat.tick(w,frame);
        check(r.unresolved.empty(),"turret test unresolved");
        if (next<expected.size() && expected[next].first==frame) {
            check(w.entities[0].turret_facing_raw==expected[next].second,"original turret interpolation");
            ++next;
        }
        check(r.fired==(frame==11?1u:0u),"turret fired outside the original facing tolerance");
    }
    check(w.entities[0].turret_facing_raw==16383 && w.entities[0].turret_facing==63,"turret did not finish the turn");
    // A turret already on target fires on the order frame.
    auto ready=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400);
    ready.entities[0].turret_facing_raw=16384;
    sim::PrototypeCombatRuntime aligned(rules,map,art,exe);
    check(aligned.attack(ready,1,2) && aligned.tick(ready,0).fired==1,"aligned turret waited");
}
// Base 006C9C00 burst branch and the 006CB2D9..006CB2F3 burst count.
void burst_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    const auto shots=[&](const char* text,std::vector<unsigned>& frames,std::vector<int>& indices) {
        rules::IniFile map;map.parse(text);
        auto w=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,4000);
        w.entities[0].turret_facing_raw=16384;
        sim::PrototypeCombatRuntime combat(rules,map,art,exe);
        check(combat.attack(w,1,2),"burst test attack rejected");
        for (unsigned frame=0;frame<150;++frame) {
            const auto r=combat.tick(w,frame);
            check(r.unresolved.empty(),"burst test unresolved");
            if (r.fired) {frames.push_back(frame);indices.push_back(*w.entities[0].burst_index);}
        }
        return w;
    };
    std::vector<unsigned> frames;std::vector<int> indices;
    shots("[120mm]\nBurst=2\n",frames,indices);
    sim::Random oracle(0);
    const int gap=oracle.ranged(3,5);
    const int reload=weapons::base_single_shot_unit_delay({65,1,false,false,false,false,1},oracle);
    check(frames.size()>=3 && frames[0]==0 && frames[1]==unsigned(gap) &&
          frames[2]==unsigned(gap+reload) && indices[0]==1 && indices[1]==0,"original Random(3,5) burst delay");
    frames.clear();indices.clear();
    shots("[120mm]\nBurst=2\n[HTNK]\nBurstDelay0=7\n",frames,indices);
    sim::Random fixed(0);
    const int fixed_reload=weapons::base_single_shot_unit_delay({65,1,false,false,false,false,1},fixed);
    check(frames.size()>=3 && frames[1]==7 && frames[2]==unsigned(7+fixed_reload),"Unit BurstDelay0");
}
// Oracle duels at middle speed (reference/REFERENCE.md). Returns the
// impact frames until the target dies.
std::vector<unsigned> duel(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe,
                           const char* shooter,world::Kind shooter_kind,const char* victim,world::Kind victim_kind,int cells) {
    rules::IniFile map;
    auto w=make_world(shooter,victim,shooter_kind,victim_kind,rules.read_integer(shooter,"Strength"),
                      rules.read_integer(victim,"Strength"));
    w.entities[1].position=sim::Coord{(50+cells)*256+128,50*256+128,0};
    w.entities[1].cell=ra2map::Cell{std::uint16_t(50+cells),50};
    w.entities[1].collision_virtual_position=w.entities[1].position;
    w.cells.at(50*512+54).heads->ground_head=0;w.cells.at(50*512+50+cells).heads->ground_head=2;
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    check(combat.attack(w,1,2),"duel attack rejected");
    std::vector<unsigned> hits;
    for (unsigned frame=0;frame<3000;++frame) {
        const auto r=combat.tick(w,frame);
        check(r.unresolved.empty(),"duel unresolved");
        for (const auto& e:r.events) if (e.kind==sim::PrototypeCombatEventKind::Damage && e.hp_after<e.hp_before) hits.push_back(frame);
        if (r.destroyed) break;
    }
    std::cout<<shooter<<" vs "<<victim<<": hits="<<hits.size();
    if (hits.size()>1) std::cout<<" mean gap="<<double(hits.back()-hits.front())/(hits.size()-1)<<" frames";
    std::cout<<" kill="<<(hits.empty()?0:hits.back())<<" frames\n";
    return hits;
}
void oracle_duel_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    // 20.7 frames/s. Grizzly 5 hits 2.93 s apart (60.7 frames), Rhino 5 hits
    // 3.19 s apart (66.0 frames), GI ~12 hits 1.06 s apart (21.9 frames).
    const auto gap=[](const std::vector<unsigned>& h){return double(h.back()-h.front())/(h.size()-1);};
    const auto mtnk=duel(rules,art,exe,"MTNK",world::Kind::Unit,"MTNK",world::Kind::Unit,4);
    check(mtnk.size()==5 && gap(mtnk)>=59 && gap(mtnk)<=63,"Grizzly duel differs from the original");
    const auto htnk=duel(rules,art,exe,"HTNK",world::Kind::Unit,"HTNK",world::Kind::Unit,4);
    check(htnk.size()==5 && gap(htnk)>=64 && gap(htnk)<=68,"Rhino duel differs from the original");
    const auto gi=duel(rules,art,exe,"E1",world::Kind::Infantry,"E1",world::Kind::Infantry,2);
    // SA ProneDamage=70%: the first hit is 15, then the prone GI takes 10.
    check(gi.size()==12 && gap(gi)>=20 && gap(gi)<=24,"GI duel differs from the original");
    // Pillbox vs GI: 4 bursts about 1.2 s (25 frames) apart in the original.
    const auto pill=duel(rules,art,exe,"GAPILL",world::Kind::Structure,"E1",world::Kind::Infantry,2);
    check(pill.size()==4 && gap(pill)>=24 && gap(pill)<=29,"Pillbox duel differs from the original");
}
void structure_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    const int strength=rules.read_integer("NAPOWR","Strength");
    check(strength>0,"installed power plant strength is missing");
    auto w=make_world("HTNK","NAPOWR",world::Kind::Unit,world::Kind::Structure,400,strength);
    sim::PrototypeCombatRuntime combat(rules,map,art,exe);
    check(combat.attack(w,1,2),"structure attack rejected");
    std::size_t killed=0;
    for (std::uint64_t frame=0;frame<3000 && !killed;++frame) {
        auto result=combat.tick(w,frame);
        if (!result.unresolved.empty()) throw std::runtime_error(result.unresolved.front());
        killed+=result.destroyed;
    }
    check(killed==1 && w.entities[1].lifetime==world::Lifetime::Destroyed &&
          w.cells.at(50*512+54).heads->ground_head==3,"structure did not die cleanly");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: prototype_combat_test GAME_DIR");
        fs::MixFileSystem files(argv[1]);files.bootstrap(fs::GameVariant::RedAlert2);files.mount("conquer.mix",false);
        auto rules=load(files,"rules.ini"),art=load(files,"art.ini");
        yard_volley_test(rules,art,std::string(argv[1])+"/game.exe","NACNST");
        yard_volley_test(rules,art,std::string(argv[1])+"/game.exe","GACNST");
        idle_defense_test(rules,art,std::string(argv[1])+"/game.exe");
        yard_defense_test(rules,art,std::string(argv[1])+"/game.exe");
        for (const auto* type : {"HTNK","DRON","NALASR"}) {
            const auto weapon=rules.read_string(type,"Primary");
            std::cout<<type<<" weapon="<<weapon<<" range="<<rules.read_string(weapon,"Range")
                     <<" ROF="<<rules.read_string(weapon,"ROF")<<" damage="<<rules.read_string(weapon,"Damage")<<'\n';
        }
        std::cout<<"TeamDelays="<<rules.read_string("General","TeamDelays")<<'\n';
        air_ground_test(rules,art,std::string(argv[1])+"/game.exe");
        fallen_house_test(rules,art,std::string(argv[1])+"/game.exe",files);
        tank_test(rules,art,std::string(argv[1])+"/game.exe");
        infantry_test(rules,art,std::string(argv[1])+"/game.exe");
        cyclic_link_test(rules,art,std::string(argv[1])+"/game.exe");
        structure_test(rules,art,std::string(argv[1])+"/game.exe");
        turret_turn_test(rules,art,std::string(argv[1])+"/game.exe");
        burst_test(rules,art,std::string(argv[1])+"/game.exe");
        oracle_duel_test(rules,art,std::string(argv[1])+"/game.exe");
        overlay_and_range_test(rules,art,std::string(argv[1])+"/game.exe");
        std::cout<<"Prototype tank, infantry, and structure combat replay passes\n";return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
