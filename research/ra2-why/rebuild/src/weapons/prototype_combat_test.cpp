#include <fs/mix_bootstrap.h>
#include <rules/ini_file.h>
#include <sim/damage_runtime.h>
#include <weapons/detonation.h>
#include <weapons/fire_timer.h>
#include <iostream>
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
    world::House b=a;b.id=102;w.houses={a,b};
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
    world::Entity tree;tree.id=3;tree.type="TREE";tree.kind=world::Kind::Terrain;tree.lifetime=world::Lifetime::Placed;
    tree.position=*target.position;tree.next=0;tree.collision_eligible=false;
    w.cells.at(50*512+50).heads->ground_head=1;
    w.cells.at(50*512+54).heads->ground_head=2;w.cells.at(50*512+54).ground_occupancy_124=0x20;
    w.entities={source,target,tree};return w;
}
void same_rng(const sim::RandomState& a,const sim::RandomState& b) {
    check(a.first==b.first && a.second==b.second && a.table==b.table,"replay RNG differs");
}
void tank_test(const rules::IniFile& rules,const rules::IniFile& art,const std::string& exe) {
    rules::IniFile map;
    auto a=make_world("HTNK","HTNK",world::Kind::Unit,world::Kind::Unit,400,400),b=a;
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
        if (frame==0) {
            check(r.fired==1 && a.entities[0].weapon_timer_start_frame==0 &&
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
    check(a.cells.at(50*512+54).heads->ground_head==3 && a.entities[2].next==0,"death changed surviving links");
    check(!one.attack(a,1,2),"dead target accepted");
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
        tank_test(rules,art,std::string(argv[1])+"/game.exe");
        infantry_test(rules,art,std::string(argv[1])+"/game.exe");
        structure_test(rules,art,std::string(argv[1])+"/game.exe");
        overlay_and_range_test(rules,art,std::string(argv[1])+"/game.exe");
        std::cout<<"Prototype tank, infantry, and structure combat replay passes\n";return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
