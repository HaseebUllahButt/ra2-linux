#include "world.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::session::world;
int main() {
    Snapshot s;
    s.random=sim::Random(42).state();
    House first; first.id=1; first.name="Player"; first.country="Americans";
    first.initial_credits=5000; first.cash=5000; first.selected_color=0; first.palette_color=3;
    s.houses.push_back(first);
    Entity a; a.id=2; a.house_id=1; a.type="AMCV";
    a.lifetime=Lifetime::Placed; a.position=sim::Coord{14080,17792,0};
    a.cell=ra2map::Cell{55,69}; a.layer=Layer::Ground; a.next=3;
    Entity b=a; b.id=3; b.next=0; b.type="MTNK";
    Entity c; c.id=4; c.house_id=1; c.type="AMCV";
    s.entities={a,b,c};
    s.creation_order={{CreatedKind::House,1},{CreatedKind::Entity,2},{CreatedKind::Entity,3},{CreatedKind::Entity,4}};
    s.cells[69*512+55].heads=weapons::CellContentHeads{2,0};
    assert(validate(s));
    auto terrain=s;
    terrain.entities[1].kind=Kind::Terrain; terrain.entities[1].house_id=0;
    terrain.entities[1].type="TREE01";
    assert(validate(terrain));
    terrain.entities[1].kind=Kind::Unit;
    assert(!validate(terrain));
    s.entities[0].category=sim::CoarseOccupantState{};
    s.entities[0].category->runtime_type=1;
    s.entities[1].category=sim::CoarseOccupantState{};
    s.entities[1].category->runtime_type=6;
    s.entities[1].category->building_flag_16c0=false;
    s.entities[1].category->building_flag_16bf=false;
    const auto phase=coarse_inputs(s,69*512+55);
    assert(phase && phase->occupants && phase->occupants->size()==2);
    assert((*phase->occupants)[0].runtime_type==1 && (*phase->occupants)[1].runtime_type==6);
    assert(!phase->usable && !phase->land_type && !phase->overlay_index && !phase->session_field_1258);
    auto unknown_heads=s; unknown_heads.cells.at(69*512+55).heads.reset();
    const auto no_links=coarse_inputs(unknown_heads,69*512+55);
    assert(no_links && !no_links->occupants);
    auto unknown_next=s; unknown_next.entities[1].next.reset();
    const auto incomplete_chain=coarse_inputs(unknown_next,69*512+55);
    assert(validate(unknown_next) && incomplete_chain && !incomplete_chain->occupants);
    auto resolved=s;
    auto& cell=resolved.cells.at(69*512+55);
    cell.usable=true; cell.land_wheel=1.0f; cell.terrain=sim::TerrainCell{};
    cell.terrain->cell={55,69}; cell.terrain->overlay_type_index=-1; cell.terrain->land_type=0;
    const auto ready=coarse_inputs(resolved,69*512+55); assert(ready);
    assert(sim::original_coarse_category(*ready).category==0);
    resolved.entities[1].category.reset();
    bool rejected=false;
    try { (void)sim::original_coarse_category(*coarse_inputs(resolved,69*512+55)); }
    catch (const std::invalid_argument&) { rejected=true; }
    assert(rejected);
    auto unknown=s; unknown.entities[1].category.reset();
    const auto incomplete=coarse_inputs(unknown,69*512+55);
    assert(incomplete && incomplete->occupants && !(*incomplete->occupants)[1].runtime_type);
    auto invalid=s; invalid.entities[1].next=2; assert(!validate(invalid));
    invalid=s; invalid.entities[1].next=77; assert(!validate(invalid));
    invalid=s; invalid.cells.begin()->second.heads->bridge_head=77; assert(!validate(invalid));
    invalid=s; invalid.cells.begin()->second.heads->ground_head=4; assert(!validate(invalid));
    invalid=s; invalid.creation_order[1].id=3; assert(!validate(invalid));
    invalid=s; invalid.entities[0].house_id=77; assert(!validate(invalid));
    invalid=s; invalid.random.second=0; assert(!validate(invalid));
    invalid=s; invalid.entities[0].position.reset(); assert(!validate(invalid));
    invalid=s; invalid.entities[0].lifetime=Lifetime::Destroyed; assert(!validate(invalid));
    invalid=s; invalid.pending_setup_cell=69*512+55; assert(!validate(invalid));
    invalid=s; invalid.pending_cell_setup=true; invalid.pending_setup_cell=512*512; assert(!validate(invalid));
    sim::Random expected_random(42);
    Stage staged(expected_random.state());
    const auto house=staged.create_player_house("Player","Americans",5000,0,512); assert(house && *house==1);
    const auto house_random=expected_random.ranged(450,1800);
    assert(staged.snapshot().houses[0].constructor_random==house_random);
    const auto unit=staged.create_unit(*house,"AMCV",1000,0,false); assert(unit && *unit==2);
    assert(validate(staged.snapshot()));
    assert(staged.snapshot().houses[0].cash==5000 && staged.snapshot().houses[0].palette_color==3);
    assert(staged.snapshot().houses[0].unit_count==1 && staged.snapshot().houses[0].unit_type_counts->at(0)==1 && staged.snapshot().houses[0].unit_fixed_counts->at(0)==1);
    assert(staged.snapshot().entities[0].lifetime==Lifetime::Created && staged.snapshot().entities[0].resolved_hp==1000);
    assert(staged.snapshot().entities[0].category && staged.snapshot().entities[0].category->runtime_type==1);
    assert(staged.snapshot().entities[0].constructor_random==static_cast<std::uint16_t>(expected_random.next()));
    assert(staged.snapshot().random.table==expected_random.table() && staged.snapshot().random.first==expected_random.first());
    auto pending=staged.snapshot(); pending.pending_cell_setup=true;
    Stage paused(pending);
    assert(!paused.create_player_house("Next","Russians",5000,1,512));
    assert(!paused.create_auxiliary_house(HouseRole::Neutral,512,{}));
    assert(!paused.create_unit(*house,"AMCV",1000,0,false));
    assert(paused.snapshot().random.table==pending.random.table &&
           paused.snapshot().random.first==pending.random.first &&
           paused.snapshot().random.second==pending.random.second);
    assert(paused.snapshot().houses.size()==pending.houses.size() &&
           paused.snapshot().entities.size()==pending.entities.size() &&
           paused.snapshot().creation_order.size()==pending.creation_order.size());
    sim::Random battle_random(17);
    Stage battle(battle_random.state());
    const auto player=battle.create_player_house("Player","Americans",5000,0,64); assert(player);
    assert(battle.snapshot().houses[0].constructor_random==battle_random.ranged(450,1800));
    assert(!battle.create_auxiliary_house(HouseRole::Special,64,5));
    assert(battle.create_auxiliary_house(HouseRole::Neutral,64,5));
    assert(battle.snapshot().houses[1].constructor_random==battle_random.ranged(450,1800));
    assert(battle.create_auxiliary_house(HouseRole::Special,64,5));
    assert(battle.snapshot().houses[2].constructor_random==battle_random.ranged(450,1800));
    assert(!battle.create_player_house("Late","Russians",5000,1,64));
    const auto battle_unit=battle.create_unit(*player,"AMCV",1000,0,false); assert(battle_unit);
    assert(battle.snapshot().entities[0].constructor_random==static_cast<std::uint16_t>(battle_random.next()));
    assert(battle.snapshot().random.table==battle_random.table() && validate(battle.snapshot()));
    std::map<std::uint32_t,CellState> cells;
    for (int y=68;y<=70;++y) for (int x=53;x<=55;++x)
    {
        auto& cell=cells[y*512+x]; cell.allocated=true; cell.neighbor_count=0;
        cell.terrain.emplace();
        cell.terrain->cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        cell.terrain->tile_index=32; cell.terrain->resolved_tile_index=32;
        cell.terrain->ramp_index=0; cell.terrain->height_level=0;
    }
    auto make_target=[&](int x,int y,int land,float speed,int category) {
        auto& cell=cells[y*512+x];
        cell.heads=weapons::CellContentHeads{};
        cell.terrain=sim::TerrainCell{};
        cell.terrain->cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        cell.terrain->tile_index=32; cell.terrain->resolved_tile_index=32;
        cell.terrain->height_level=0; cell.terrain->ramp_index=0;
        cell.terrain->bridge=false; cell.terrain->overlay_type_index=-1;
        cell.terrain->land_type=land; cell.terrain->floor_world_height=0;
        cell.unit_land_speed=speed; cell.land_wheel=speed;
        cell.ground_occupancy_124=0; cell.cell_flag_100=false;
        cell.usable=true; cell.coarse_category=static_cast<std::uint8_t>(category);
        cell.cell_flag_200=false; cell.cell_flag_40000=false;
        cell.cell_field_54=-1; cell.cell_field_58=-1; cell.tube_field_116=-1;
    };
    make_target(54,69,0,1.0f,0);
    make_target(56,69,2,0.0f,4);
    assert(battle.install_base_cells({80,80,{2,2,76,72}},std::move(cells),0,{}));
    BaseUnitProfile profile;
    profile.restricted_land=-1; profile.movement_mode=-1; profile.placement_mode=2;
    profile.speed_type=1;
    profile.level_height=104;
    profile.type_flag_aa0=false; profile.unit_flag_32c=false; profile.linked_team=false;
    profile.type_flag_205=true; profile.unit_flag_32a=false;
    profile.type_flag_bb0=false; profile.type_flag_bb1=false;
    auto unknown_occupancy=battle.snapshot();
    unknown_occupancy.cells.at(69*512+54).ground_occupancy_124.reset();
    Stage guarded_occupancy(unknown_occupancy);
    assert(guarded_occupancy.place_base_unit(*battle_unit,{54,69},32,profile)==PlacementResult::Unresolved);
    auto unknown_tracking=battle.snapshot();
    unknown_tracking.entities[0].occupancy_tracking.reset();
    Stage guarded_tracking(unknown_tracking);
    assert(guarded_tracking.place_base_unit(*battle_unit,{54,69},32,profile)==PlacementResult::Unresolved);
    auto unknown_bridge_flag=battle.snapshot();
    unknown_bridge_flag.cells.at(69*512+54).cell_flag_100.reset();
    Stage guarded_bridge(unknown_bridge_flag);
    assert(guarded_bridge.place_base_unit(*battle_unit,{54,69},32,profile)==PlacementResult::Unresolved);
    auto missing=profile; missing.linked_team.reset();
    assert(battle.place_base_unit(*battle_unit,{54,69},32,missing)==PlacementResult::Unresolved);
    auto train=profile; train.type_flag_aa0=true;
    assert(battle.place_base_unit(*battle_unit,{54,69},32,train)==PlacementResult::Unresolved);
    assert(battle.snapshot().entities[0].on_map==false);
    assert(battle.place_base_unit(*battle_unit,{56,69},32,profile)==PlacementResult::Blocked);
    assert(battle.snapshot().entities[0].on_map==true && battle.snapshot().entities[0].lifetime==Lifetime::Created);
    assert(battle.snapshot().cells.at(69*512+56).ground_occupancy_124==0);
    assert(battle.place_base_unit(*battle_unit,{54,69},32,profile)==PlacementResult::Placed);
    const auto& placed=battle.snapshot().entities[0];
    assert(placed.lifetime==Lifetime::Placed && placed.hull_facing==32 && placed.turret_facing==32);
    assert(battle.snapshot().cells.at(69*512+54).heads->ground_head==*battle_unit);
    assert(battle.snapshot().cells.at(69*512+54).ground_occupancy_124==0x20);
    assert(battle.snapshot().pending_cell_setup);
    assert(battle.snapshot().pending_setup_cell==69*512+54);
    assert(!battle.snapshot().cells.at(69*512+54).coarse_category);
    assert(!coarse_inputs(battle.snapshot(),69*512+54));
    assert(battle.place_base_unit(*battle_unit,{56,69},32,profile)==PlacementResult::Unresolved);
    assert(battle.snapshot().houses[0].placed_unit_counts->at(0)==1);
    for (const auto& direction:std::vector<std::pair<int,int>>{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}})
        assert(battle.snapshot().cells.at((69+direction.second)*512+54+direction.first).neighbor_count==1);
    assert(battle.snapshot().random.table==battle_random.table() && validate(battle.snapshot()));
    sim::TheaterInputs theater; theater.theater_name="TEMPERATE";
    theater.tile_sets={{0,33}}; theater.settings.parse("[General]\n");
    theater.resolved_metadata=[](std::uint32_t index,std::uint8_t subtile)->std::optional<sim::TileMetadata> {
        if (index!=32 || subtile!=0) return {};
        return sim::TileMetadata{0,0,true};
    };
    sim::CellSetupStatic guard;
    guard.receiver_tile_index=32; guard.animation_index=-1;
    guard.shadow_enabled=false; guard.bounds_height=30;
    const auto before_setup=battle.snapshot();
    auto missing_bounds=guard; missing_bounds.bounds_height.reset();
    const auto incomplete_setup=battle.complete_pending_cell_setup(missing_bounds,theater,0,{{0,1.0f}});
    assert(incomplete_setup.status==sim::CellSetupStatus::NeedsInput);
    assert(battle.snapshot().pending_cell_setup && battle.snapshot().random.table==before_setup.random.table &&
           battle.snapshot().entities.size()==before_setup.entities.size());
    const auto completed=battle.complete_pending_cell_setup(guard,theater,0,{{0,1.0f}});
    assert(completed.status==sim::CellSetupStatus::Complete);
    const auto& after=battle.snapshot();
    const auto& done=after.cells.at(69*512+54);
    assert(!after.pending_cell_setup && !after.pending_setup_cell && done.cell_field_11d==0 &&
           done.coarse_category==0 && done.coarse_cache_height==0 && done.hierarchy_cache_height==0);
    assert(done.heads->ground_head==*battle_unit && done.ground_occupancy_124==0x20 &&
           after.entities[0].lifetime==Lifetime::Placed && after.random.table==before_setup.random.table &&
           after.random.first==before_setup.random.first && after.random.second==before_setup.random.second);
    assert(!staged.create_unit(99,"AMCV",1000,0,false));
    assert(!staged.create_unit(*house,"AMCV",1000,512,false));
    assert(!staged.create_player_house("Bad","Americans",5000,8,512));
    assert(staged.snapshot().random.table==expected_random.table() && staged.snapshot().random.first==expected_random.first());
    std::cout<<"staged snapshot link and random validation passed\n";
}
