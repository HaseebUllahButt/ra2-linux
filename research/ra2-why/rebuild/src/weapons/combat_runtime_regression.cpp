#include "cannon_line.h"
#include "cannon_range.h"
#include "fire_eligibility.h"
#include "fire_timer.h"
#include "techno_damage.h"
#include "projectile_damage.h"
#include "muzzle.h"
#include "detonation.h"
#include "unit_death.h"
#include <sim/damage_runtime.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>
#include <string>
using namespace ra2;
using namespace ra2::weapons;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::uint32_t hash(const sim::RandomState& state) {
    std::uint32_t h=2166136261u; for (auto word:state.table) h=(h^word)*16777619u; return h;
}
OrdinaryUnitFireInput ordinary() {
    OrdinaryUnitFireInput in; auto& p=in.prefix;
    p.target_present=true;p.shooter_virtual_1c0=false;p.shooter_virtual_1bc=false;p.shooter_flag_325=false;
    p.shooter_virtual_1c4=false;p.target_is_object=true;p.target_cell_resolved=true;p.target_limbo=false;
    p.target_flag_210=false;p.target_virtual_64=0;p.shooter_flag_7a=false;p.shooter_virtual_324=false;
    in.shooter_is_unit=true;in.target_is_unit=true;in.primary_weapon_index_zero=true;in.weapon_present=true;
    in.weapon_special_flags_clear=true;in.alternate_weapon_effects_clear=true;in.warhead_special_flags_clear=true;
    in.warhead_present=true;in.target_virtual_1bc=false;in.target_virtual_54=false;in.target_cell_land_type=0;
    in.target_virtual_50=false;in.barrel_override_active=false;in.timer=FireTimer{-1,0};in.frame=100;
    in.ammo_field_254=1;in.weapon_flag_10f=true;in.shooter_state_1b8=0;in.shooter_type_af2=false;in.warhead_armor_verse=1;in.target_flag_325=false;
    in.matching_bridge_flags=true;in.shooter_type_b47=false;return in;
}
void check_fire_and_damage() {
    auto fire=ordinary();
    check(base_ordinary_unit_fire_check(fire).rejection==0,"ordinary Unit fire check");
    fire.timer=FireTimer{100,65};fire.frame=164;
    check(base_ordinary_unit_fire_check(fire).rejection==3,"fire timer before expiry");
    fire.frame=165;check(base_ordinary_unit_fire_check(fire).rejection==0,"fire timer expiry");
    fire.shooter_state_1b8=1;check(base_ordinary_unit_fire_check(fire).rejection==9,"DecloakToFire state");
    fire.shooter_state_1b8=0;
    fire.alternate_weapon_effects_clear.reset();
    check(!base_ordinary_unit_fire_check(fire).rejection,"unknown secondary slot accepted");
    check(!base_fire_timer_ready({-1,65},100) && base_fire_timer_ready({-1,0},100),"stopped timer");
    sim::Random rng(0); SingleShotDelayInput delay{65,1,false,false,false,false,1};
    check(base_single_shot_unit_delay(delay,rng)==67,"original reload delay");
    auto state=rng.state();check(state.first==2 && state.second==105 && hash(state)==1127516651u,"original reload RNG state");
    sim::Random replay(0);check(base_single_shot_unit_delay(delay,replay)==67 && hash(replay.state())==hash(state),"reload replay");
    ProjectileDamageInput shot{90,false,false,1.2,0.5,false,false,false,false,1};
    check(base_projectile_damage(shot)==53,"original projectile damage rounding");
    shot.house_firepower=std::numeric_limits<double>::infinity();
    check(!base_projectile_damage(shot),"unknown projectile firepower accepted");
    UnitDeathTriggerState death{true,true,-1};std::vector<int> events;
    const auto trigger=[&](int event,UnitDeathTriggerState& state) {
        events.push_back(event);if (event==7) state.active=false;
    };
    check(base_unit_death_trigger_prefix(death,true,{},trigger) && events==std::vector<int>({7,0x30,0x1d}),
          "original Unit death trigger order");
    check(base_unit_parent_death_trigger_prefix(death,true,trigger) && events.size()==3,
          "inactive Unit parent death trigger");
    death={true,true,-1};events.clear();
    check(base_unit_parent_death_trigger_prefix(death,true,trigger) && events==std::vector<int>({6,4}),
          "Unit parent death trigger order");
    death={true,true,0};events.clear();
    check(!base_unit_death_trigger_prefix(death,true,{},trigger) && events.empty(),"unknown Unit trigger check accepted");
    TechnoDamageInput d{};d.raw_damage=100;d.house_armor_multiplier=2;d.object_armor_multiplier=1;
    d.veteran_armor_multiplier=1;d.attacker_present=true;d.warhead_present=true;
    auto result=base_techno_damage_prefix(d);
    check(result.stage==TechnoDamageStage::ObjectDamage && result.damage==50,"Techno damage multiplier");
    d.invulnerable_virtual=true;result=base_techno_damage_prefix(d);
    check(result.stage==TechnoDamageStage::InvulnerabilityEffect && result.damage==50,"invulnerability callback order");
    d.invulnerable_virtual=false;d.house_armor_multiplier=std::numeric_limits<double>::infinity();
    bool rejected=false;try { (void)base_techno_damage_prefix(d); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected,"nonfinite Techno armor accepted");d.house_armor_multiplier=2;
    d.type_has_side_effect_5d0=true;
    check(base_techno_damage_prefix(d).stage==TechnoDamageStage::NeedsTypeSideEffect,"unknown Techno effect accepted");
}
void check_world(const sim::OriginalDistance& distance) {
    session::world::Snapshot world;const sim::Coord impact{50*256+128,50*256+128,0};
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        session::world::CellState cell;cell.allocated=true;cell.heads.emplace();cell.cell_flag_100=false;
        cell.terrain.emplace();cell.terrain->floor_world_height=0;cell.terrain->ramp_index=0;cell.terrain->overlay_type_index=-1;
        world.cells.emplace((50+dy)*512+50+dx,std::move(cell));
    }
    const unsigned center=50*512+50;
    world.cells.at(center).heads->ground_head=2;
    world.cells.at(49*512+51).heads->ground_head=1;
    session::world::Entity a;a.id=2;a.kind=session::world::Kind::Unit;a.lifetime=session::world::Lifetime::Placed;
    a.position=impact;a.next=1;
    auto b=a;b.id=1;b.position=sim::Coord{impact.x+5,impact.y,0};b.next=0;
    auto source=a;source.id=3;source.next=0;
    world.entities={a,b,source};
    const auto aim=sim::base_stationary_unit_aim(world,3,2,false);
    check(aim && aim->x==impact.x && aim->y==impact.y && aim->z==impact.z,"stationary Unit aim");
    check(!sim::base_stationary_unit_aim(world,3,2,{}) && !sim::base_stationary_unit_aim(world,3,2,true),
          "unknown or moving Unit aim accepted");
    const auto area=[](const session::world::Entity& e)->std::optional<sim::AreaUnitProperties> {
        return sim::AreaUnitProperties{true,e.position,false,false,false};
    };
    auto collected=sim::base_small_ground_area_candidates(world,impact,0.3f,3,false,area,distance);
    check(collected.candidates && collected.candidates->size()==3,"area candidate count");
    const auto& c=*collected.candidates;
    check(c[0].id==2 && c[0].distance==0 && c[1].id==1 && c[1].distance==5 &&
          c[2].id==1 && c[2].distance==5,"area order and shared links");
    sim::AreaDamageCursor cursor{{impact,3,8,90,true},76,*collected.candidates,0,{}};
    bool alive=true;
    const auto recipient=[&](std::uint32_t id)->std::optional<ResolvedImpactRecipient> {
        ResolvedImpactRecipient state{};state.target_id=id;state.original_type=1;
        state.health=(id==1 && !alive)?0:400;state.active=true;state.receives_damage=true;
        state.scenario_blocks_damage=true; // Entry gate already passed before collection.
        return state;
    };
    auto next=sim::base_next_area_damage(cursor,recipient);
    check(next.status==sim::AreaDamageStep::Request && next.request && next.request->target_id==2,
          "first ordered damage callback");
    check(sim::base_next_area_damage(cursor,recipient).request->target_id==2 &&
          !sim::base_complete_area_damage_request(cursor,1) && sim::base_complete_area_damage_request(cursor,2),
          "damage callback acknowledgement");
    next=sim::base_next_area_damage(cursor,recipient);
    check(next.request && next.request->target_id==1,"second ordered damage callback");
    alive=false;check(sim::base_complete_area_damage_request(cursor,1),"damage callback completion");
    check(sim::base_next_area_damage(cursor,recipient).status==sim::AreaDamageStep::Complete,
          "damage recipient state was not refreshed");
    sim::AreaDamageCursor blocked{{impact,3,8,90,true},76,*collected.candidates,0,{}};
    auto unknown=[](std::uint32_t)->std::optional<ResolvedImpactRecipient> {return {};};
    check(sim::base_next_area_damage(blocked,unknown).status==sim::AreaDamageStep::NeedsInput && blocked.next==0,
          "unknown damage recipient advanced");
    auto missing=world;missing.entities[0].next.reset();
    check(!sim::base_small_ground_area_candidates(missing,impact,0.3f,3,false,area,distance).candidates,"unknown area link accepted");
    const auto collision=[](const session::world::Entity& e,std::uint32_t)->std::optional<sim::CollisionProperties> {
        return sim::CollisionProperties{true,e.position,false};
    };
    auto hit=sim::base_world_projectile_collision(world,center,impact,3,false,collision,distance);
    check(hit.collision && hit.collision->selected_id==2 && hit.collision->collision,"ordered projectile collision");
    check(!sim::base_world_projectile_collision(missing,center,impact,3,false,collision,distance).collision,"unknown collision link accepted");
    auto allied=[](const session::world::Entity& e,std::uint32_t)->std::optional<sim::CollisionProperties> {
        return sim::CollisionProperties{true,e.position,true};
    };
    hit=sim::base_world_projectile_collision(world,center,impact,3,false,allied,distance);
    check(hit.collision && hit.collision->selected_id==2 && !hit.collision->collision,"allied selected object must mask later objects");
    world.pending_cell_setup=true;
    check(!sim::base_world_projectile_collision(world,center,impact,3,false,collision,distance).collision,"pending setup accepted");
}
void check_projectile(const std::string& path,const sim::OriginalDistance& distance) {
    const CannonLineLookup lookup_line=[](std::int16_t x,std::int16_t y)->std::optional<CannonLineCell> {
        if (x<0 || x>=512 || y<0 || y>=512) return {};
        const int h=(x*37+y*73+17)&255, level=h%7-3, bit=(h/7)%2, ov=(h/14)%7;
        const int overlay=ov==0?0:ov==1?2:ov==2?26:-1;
        return CannonLineCell{{static_cast<unsigned>(y*512+x+1),level+4*bit,overlay!=-1,false,false},level,overlay};
    };
    CannonLineInput line{{12800,12800,0},{15360,15360,0},true,true,false,false};
    auto trace=base_cannon_line_trace(line,lookup_line);
    check(trace.status==TerrainTraceStatus::Hit && trace.cell_id==26164,"original Cannon line trace");
    const CannonLineLookup missing_line=[](std::int16_t,std::int16_t)->std::optional<CannonLineCell> {return {};};
    check(base_cannon_line_trace(line,missing_line).status==TerrainTraceStatus::NeedsCell,"unknown Cannon line cell accepted");
    // The exception must read the returned hit Cell. This intentionally
    // varies the lookup to detect use of the earlier sampled-cell snapshot.
    int calls=0;
    CannonLineLookup changing=[&](std::int16_t,std::int16_t)->std::optional<CannonLineCell> {
        ++calls;
        if (calls==4) return CannonLineCell{{7,4,true,false,false},0,2};
        return CannonLineCell{{static_cast<unsigned>(calls==2?2:1),0,false,false,false},0,0};
    };
    CannonLineInput exception{{0,0,0},{512,0,0},true,true,false,true};
    check(base_cannon_line_trace(exception,changing).status==TerrainTraceStatus::Clear,"line exception used the wrong cell");
    check(base_weapon_range_from_cells(5.75,0)==1472 && base_weapon_range_from_cells(-1.0,19)==19,
          "original weapon range conversion");
    CannonRangeInput range_input{{0,0,0},{512,0,0},1024,0,6,true,false};
    check(base_cannon_range(range_input,distance)==CannonRangeStatus::InRange,"original Cannon range");
    range_input.target.x=1030;
    check(base_cannon_range(range_input,distance)==CannonRangeStatus::OutOfRange,"Cannon maximum range");
    range_input.target.x=512;range_input.line_blocked.reset();
    check(base_cannon_range(range_input,distance)==CannonRangeStatus::NeedsInput,"unknown Cannon line trace accepted");
    FlatUnitMuzzleInput muzzle{{12800,12800,0},0,16384,{{150,0,100}},0,0,true};
    const auto muzzle_point=base_flat_unit_muzzle(muzzle);
    check(muzzle_point && muzzle_point->x==12949 && muzzle_point->y==12800 && muzzle_point->z==100,
          "original HTNK muzzle point");
    muzzle.flat_drive_locomotor.reset();
    check(!base_flat_unit_muzzle(muzzle),"unknown locomotor muzzle accepted");
    UnitCannonImpactInput impact{{0,0,0},true,sim::Coord{41,0,0},true};
    auto adjusted=base_unit_cannon_impact_position(impact,distance);
    check(adjusted && adjusted->x==41,"original Unit impact adjustment");
    impact.unit_position=sim::Coord{80,0,0};adjusted=base_unit_cannon_impact_position(impact,distance);
    check(adjusted && adjusted->x==0,"distant Unit impact adjustment");
    impact.ordinary_ground_target.reset();
    check(!base_unit_cannon_impact_position(impact,distance),"unknown impact state accepted");
    sim::Random impact_rng(0);
    check(!base_complete_single_cluster_scatter({},impact_rng) && impact_rng.first()==0,"unknown impact changed RNG");
    check(base_complete_single_cluster_scatter(true,impact_rng) && impact_rng.first()==2 &&
          impact_rng.second()==105 && hash(impact_rng.state())==1127516651u,"original impact RNG order");
    const auto angles=BaseBallisticAngles::from_executable(path);
    const std::array<std::int32_t,3> delta{{512,128,0}};
    const int range=distance.between({0,0,0},{512,128,0},true);
    const int speed=base_ballistic_launch_speed(range,6,distance);
    const auto launch=angles.tank_launch(speed,delta,6,false);
    check(launch && speed==61 && launch->limited_speed==61 && launch->heading==18880 && launch->pitch==11081,"original Cannon launch");
    FlatCannonTickInput tick{};tick.arc={{0,0,2000},launch->velocity,6};tick.target={100000,100000,0};
    tick.height_unit=104;tick.height_before=500;tick.height_after=500;tick.source_id=1;
    tick.proposed_building_id=0;tick.target_building_id=0;tick.building_intercepts=false;
    tick.position_in_map=true;tick.cell_objects=std::vector<ProjectileCellObject>{};tick.terrain.destination_cell_id=2;
    const TerrainTraceLookup lookup=[](std::int16_t,std::int16_t) {
        return std::optional<TerrainTraceCell>(TerrainTraceCell{1,0,false,false,false});
    };
    const std::array<std::array<int,3>,8> expected{{{{51,12,2023}},{{102,24,2040}},{{153,36,2051}},{{204,48,2056}},
                                                   {{255,60,2055}},{{306,72,2048}},{{357,84,2035}},{{408,96,2016}}}};
    sim::FlatProjectile projectile{tick.arc,tick.target,tick.source_id};
    for (const auto& p:expected) {
        check(sim::base_advance_flat_projectile(projectile,tick,lookup,distance)==FlatCannonTickStatus::Continue,"projectile tick");
        check(projectile.arc.position==p,"original projectile replay position");tick.arc=projectile.arc;
    }
    const auto before=projectile;tick.cell_objects.reset();
    check(sim::base_advance_flat_projectile(projectile,tick,lookup,distance)==FlatCannonTickStatus::NeedsInput &&
          projectile.arc.position==before.arc.position && projectile.arc.velocity==before.arc.velocity,"unknown projectile tick changed state");
    tick.cell_objects=std::vector<ProjectileCellObject>{};tick.position_in_map=false;
    check(sim::base_advance_flat_projectile(projectile,tick,lookup,distance)==FlatCannonTickStatus::Remove &&
          projectile.phase==sim::FlatProjectilePhase::RemovalPending,"projectile removal callbacks skipped");
    check(sim::base_advance_flat_projectile(projectile,tick,lookup,distance)==FlatCannonTickStatus::NeedsInput,
          "pending projectile advanced");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: combat_runtime_regression GAME_EXE");
        const auto distance=sim::OriginalDistance::from_executable(argv[1]);
        check_fire_and_damage();check_world(distance);check_projectile(argv[1],distance);
        std::cout << "Combat runtime fixed original regression cases pass\n";return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n';return 1; }
}
