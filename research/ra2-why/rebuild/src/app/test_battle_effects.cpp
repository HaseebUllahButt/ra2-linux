#include "battle_effects.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace ra2;
render::Bitmap blank(){return {160,120,std::vector<std::uint8_t>(160*120*4)};}
bool changed(const render::Bitmap& b){return std::any_of(b.rgba.begin(),b.rgba.end(),[](auto c){return c;});}
int main(int argc,char** argv) {
    assert(argc==2);
    fs::MixFileSystem files(argv[1]);files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);files.mount_overlays();files.mount_resources();
    app::BattleEffects effects(files);
    session::world::Snapshot world;
    session::world::Entity a,b;
    a.id=1;a.position=sim::Coord{40,60,0};a.resolved_hp=100;a.target_id=2;
    b.id=2;b.position=sim::Coord{110,60,0};b.resolved_hp=100;
    world.entities={a,b};
    const app::BattleEffects::Project project=[](sim::Coord p){return app::EffectPoint{p.x,p.y};};
    effects.observe(world,{},0,project);
    auto image=blank();effects.draw(image,0,0,0);assert(!changed(image));
    world.entities[0].weapon_timer_start_frame=1;world.entities[1].resolved_hp=75;
    effects.observe(world,{},1,project);
    assert(effects.active()==3);
    image=blank();effects.draw(image,0,0,1);assert(changed(image));
    // Both ends must show feedback, even for direct-hit small arms.
    bool left=false,right=false;
    for(int y=0;y<120;++y)for(int x=0;x<160;++x)if(image.rgba[(y*160+x)*4+3]) {
        if(x<70)left=true;
        if(x>80)right=true;
    }
    assert(left&&right);
    effects.observe(world,{},1,project);assert(effects.active()==3);
    auto paused=blank();effects.draw(paused,0,0,1);assert(image.rgba==paused.rgba);
    auto clipped=blank();effects.draw(clipped,10000,10000,1);assert(!changed(clipped));
    world.entities[1].resolved_hp=0;effects.observe(world,{},2,project);
    assert(effects.active()==4);
    effects.observe(world,{},1000,project);assert(effects.active()==0);
    image=blank();effects.draw(image,0,0,1000);assert(!changed(image));
    sim::PrototypeCombatProjectile shot;shot.flight.arc.position={80,50,0};
    effects.observe(world,{shot},1001,project);
    image=blank();effects.draw(image,0,0,1001);assert(image.rgba[(50*160+80)*4+3]==255);
    // GameLoop presents before update, then advances its frame counter.
    auto next_present=blank();effects.draw(next_present,0,0,1002);
    assert(next_present.rgba==image.rgba);
    auto paused_shell=blank();effects.draw(paused_shell,0,0,1002);
    assert(paused_shell.rgba==next_present.rgba);
    auto stale_shell=blank();effects.draw(stale_shell,0,0,1003);
    assert(!changed(stale_shell));
    effects.observe(world,{},1002,project);
    image=blank();effects.draw(image,0,0,1002);assert(!changed(image));
    app::BattleEffects deaths(files);
    world.entities[0].weapon_timer_start_frame.reset();
    world.entities[1].resolved_hp=100;
    deaths.observe(world,{},0,project);
    world.entities[1].resolved_hp=0;
    deaths.observe(world,{},1,project);
    assert(deaths.active()==1);
    image=blank();deaths.draw(image,0,0,1);assert(changed(image));
    deaths.observe(world,{},1000,project);assert(deaths.active()==0);
    // Setup and the first combat tick can both use frame zero.
    app::BattleEffects first_tick(files);
    world.entities[1].resolved_hp=100;
    first_tick.observe(world,{},0,project);
    world.entities[0].weapon_timer_start_frame=0;
    world.entities[1].resolved_hp=75;
    first_tick.observe(world,{},0,project);assert(first_tick.active()==3);
    image=blank();first_tick.draw(image,0,0,0);assert(changed(image));
    first_tick.observe(world,{},0,project);assert(first_tick.active()==3);
    paused=blank();first_tick.draw(paused,0,0,0);assert(image.rgba==paused.rgba);
    // Deployment removes the MCV but preserves its positive health.
    app::BattleEffects deploy(files);
    world.entities[0].weapon_timer_start_frame.reset();
    world.entities[0].type="AMCV";
    world.entities[0].lifetime=session::world::Lifetime::Placed;
    deploy.observe(world,{},0,project);
    world.entities[0].lifetime=session::world::Lifetime::Destroyed;
    world.entities[0].on_map=false;world.entities[0].object_field_76=1;
    world.entities[0].active=false;world.entities[0].in_limbo=true;
    deploy.observe(world,{},1,project);assert(deploy.active()==0);
    image=blank();deploy.draw(image,0,0,1);assert(!changed(image));
    deploy.observe(world,{},1,project);assert(deploy.active()==0);
    world.entities[1].resolved_hp=0;
    // Observation must not change health, firing timers, or projectile data.
    assert(world.entities[1].resolved_hp==0&&!world.entities[0].weapon_timer_start_frame);
    assert(shot.flight.arc.position[0]==80);
    // Explicit events preserve firing feedback when either entity is absent.
    rules::IniFile weapon_rules;
    weapon_rules.parse("[LaserTest]\nIsLaser=yes\n[BoltTest]\nIsElectricBolt=yes\n[TankTest]\nProjectile=Cannon\n[Cannon]\nArcing=yes\n");
    session::world::Snapshot absent;
    sim::PrototypeCombatEvent event;
    event.kind=sim::PrototypeCombatEventKind::Shot;
    event.source_position={40,60,0};event.target_position={110,60,0};event.weapon="LaserTest";
    std::vector<sim::PrototypeCombatEvent> events{event};
    app::BattleEffects laser(files);
    laser.observe(absent,{},0,project,&events,&weapon_rules);
    assert(laser.active()==2);
    image=blank();laser.draw(image,0,0,1);
    const auto middle=(60*160+75)*4;
    assert(image.rgba[middle]==255 && image.rgba[middle+1]==65 && image.rgba[middle+2]==55);
    laser.observe(absent,{},0,project,&events,&weapon_rules);assert(laser.active()==2);
    app::BattleEffects bolt(files);
    events[0].weapon="BoltTest";
    bolt.observe(absent,{},0,project,&events,&weapon_rules);
    image=blank();bolt.draw(image,0,0,1);
    const auto bend=(62*160+75)*4;
    assert(image.rgba[bend]==110 && image.rgba[bend+2]==255);
    app::BattleEffects tank(files);
    events[0].weapon="TankTest";
    tank.observe(absent,{},0,project,&events,&weapon_rules);
    assert(tank.active()==1); // A simulated shell supplies the moving projectile.
    events[0].kind=sim::PrototypeCombatEventKind::Death;
    tank.observe(absent,{},1,project,&events,&weapon_rules);assert(tank.active()==2);
    image=blank();tank.draw(image,0,0,1);assert(changed(image));
    std::cout<<"base combat pixels, clipping, pause, death, expiry, and shell checks passed\n";
}
