#include "setup.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=2 && !(argc==3 && (std::string(argv[2])=="--cells" || std::string(argv[2])=="--starts" ||
                                 std::string(argv[2])=="--overlay-data" || std::string(argv[2])=="--overlay-data-cells" ||
                                 std::string(argv[2])=="--overlay-scan"))) return 2;
    if (argc==3 && std::string(argv[2])=="--starts") {
        int x,y;
        while (std::cin>>x>>y) {
            if (x< -32768 || x>32767 || y< -32768 || y>32767) return 2;
            const auto position=battle_start_position({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});
            std::cout<<position.x<<' '<<position.y<<' '<<position.z<<'\n';
        }
        return 0;
    }
    auto assets=BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    assert(assets.rules().read_string("AMCV","Strength")=="1000");
    assert(assets.rules().read_list("General","BaseUnit").size()>=2);
    assert(assets.rules().key_count("VehicleTypes")==55);
    assert(assets.art_ini().key_count("MCV")>0);
    assert(assets.mode_catalog().find("Battle","1") && assets.battle_rules_name()=="MPBattle.ini");
    assert(assets.battle_rules().read_string("MultiplayerDialogSettings","AlliesAllowed")=="yes");
    assert(assets.geometry().width==100 && assets.geometry().height==100);
    assert(assets.overlays_in_scan_order().size()==407);
    if (argc==3 && std::string(argv[2])=="--overlay-scan") {
        for (const auto& input:assets.overlays_in_scan_order())
            std::cout<<input.cell.x<<' '<<input.cell.y<<' '<<int(input.type_index)<<'\n';
        return 0;
    }
    if (argc==3 && std::string(argv[2])=="--overlay-data") {
        const auto& bytes=assets.map().packed_terrain().overlay_data;
        std::cout.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        return std::cout?0:1;
    }
    const auto& points=assets.map().waypoints();
    const auto point=std::find_if(points.begin(),points.end(),[](const auto& p){return p.index==0;});
    assert(point!=points.end() && point->cell.x==135 && point->cell.y==80);
    const auto& cells=assets.cells_in_original_order();
    assert(cells.size()==19900);
    const auto cell=std::find_if(cells.begin(),cells.end(),[](const auto& c){return c.cell.x==135 && c.cell.y==80;});
    assert(cell!=cells.end() && cell->packed_tile==131 && cell->translated_tile==131 && cell->height==4);
    assert(cell->packed_overlay && *cell->packed_overlay==255);
    const auto& metadata=assets.theater().resolved_metadata(*cell->translated_tile,cell->subtile);
    assert(metadata.present && metadata.terrain==13 && metadata.ramp==0);
    const auto loaded=read_base_packed_map(assets,{});
    assert(loaded && loaded->cells.size()==19900 && loaded->cell_order.size()==19900);
    if (argc==3 && std::string(argv[2])=="--cells") {
        for (const auto& item:loaded->cells) {
            const auto& c=item.second;
            assert(c.terrain && c.terrain->resolved_tile_index && c.terrain->height_level &&
                   c.cell_field_54 && c.cell_field_58 && c.tube_field_116 && c.neighbor_count && c.cell_flag_100 && c.heads);
            std::cout<<item.first<<' '<<*c.terrain->resolved_tile_index<<' '<<*c.terrain->height_level<<' '
                     <<*c.cell_field_54<<' '<<*c.cell_field_58<<' '<<*c.tube_field_116<<' '
                     <<int(*c.neighbor_count)<<' '<<int(*c.cell_flag_100)<<' '<<c.heads->ground_head<<' '<<c.heads->bridge_head<<'\n';
        }
        return 0;
    }
    auto wrong_order=*loaded;
    std::swap(wrong_order.cell_order[0],wrong_order.cell_order[1]);
    assert(!packed_map_snapshot(wrong_order,sim::Random(42).state()));
    const auto partial=packed_map_snapshot(*loaded,sim::Random(42).state());
    assert(partial && partial->setup_progress==SetupProgress::PackedMapRead && partial->cell_order &&
           partial->cell_order->size()==19900 && partial->entities.empty());
    auto overlay_fixture=*partial;
    assert(!apply_base_overlay_data(overlay_fixture,assets));
    assert(overlay_fixture.setup_progress==SetupProgress::PackedMapRead);
    // Prior Overlay object results are supplied here, not reconstructed.
    overlay_fixture.setup_progress=SetupProgress::OverlayObjectsRead;
    assert(apply_base_overlay_data(overlay_fixture,assets));
    assert(overlay_fixture.setup_progress==SetupProgress::OverlayDataRead &&
           overlay_fixture.random.table==partial->random.table && overlay_fixture.random.first==partial->random.first &&
           overlay_fixture.random.second==partial->random.second);
    const auto& raw_data=assets.map().packed_terrain().overlay_data;
    for (const auto& cell:overlay_fixture.cells) {
        if (raw_data.empty()) assert(!cell.second.overlay_data_11e);
        else assert(cell.second.overlay_data_11e==raw_data.at(cell.first));
    }
    if (argc==3 && std::string(argv[2])=="--overlay-data-cells") {
        for (const auto& cell:overlay_fixture.cells)
            std::cout<<cell.first<<' '<<(cell.second.overlay_data_11e?int(*cell.second.overlay_data_11e):-1)<<'\n';
        return 0;
    }
    const auto& target=loaded->cells.at(80u*512+135);
    assert(target.terrain && target.terrain->resolved_tile_index==131 && target.terrain->height_level==4);
    assert(target.terrain->bridge==false && !target.terrain->overlay_type_index && !target.terrain->land_type);
    assert(target.heads && !target.heads->ground_head && !target.heads->bridge_head);
    assert(!target.ground_occupancy_124 && target.neighbor_count==0 && target.cell_flag_100==false);
    // These are still unknown until their actual later load phases run.
    assert(!target.cell_flag_200 && !target.cell_flag_40000 && !target.bridge_occupancy_128);
    // The asset data below is real. Runtime fields marked here are supplied
    // fixtures, because the Overlay and object phases are still incomplete.
    {
        auto houses=construct_house_fragments(sim::Random(42).state(),{{"P","Americans",5000,0,0}},55,7);
        assert(houses);
        Stage stage(*houses);
        std::map<std::uint32_t,CellState> fixture;
        for (const auto& source:cells) {
            const int dx=int(source.cell.x)-135,dy=int(source.cell.y)-80;
            if (!((dx>=-1 && dx<=1 && dy>=-1 && dy<=1) || (dx==2 && dy==2))) continue;
            assert(source.packed_overlay && *source.packed_overlay==255 && source.translated_tile);
            auto& c=fixture[std::uint32_t(source.cell.y)*512+source.cell.x];
            c.allocated=true; c.neighbor_count=0; c.terrain.emplace();
            c.terrain->cell=source.cell; c.terrain->tile_index=*source.packed_tile;
            c.terrain->resolved_tile_index=*source.translated_tile; c.terrain->subtile=source.subtile;
            c.terrain->height_level=source.height<128 ? int(source.height) : int(source.height)-256;
            const auto& info=assets.theater().resolved_metadata(*source.translated_tile,source.subtile);
            c.terrain->ramp_index=info.ramp; c.terrain->tmp_terrain=info.terrain;
        }
        assert(fixture.size()==10);
        auto& c=fixture.at(80u*512+135);
        c.heads=weapons::CellContentHeads{}; c.terrain->bridge=false;
        c.terrain->overlay_type_index=-1; c.terrain->land_type=0; c.terrain->floor_world_height=416;
        c.unit_land_speed=1.0f; c.land_wheel=1.0f; c.usable=true; c.coarse_category=0;
        c.ground_occupancy_124=0; c.cell_flag_100=false;
        c.cell_field_54=-1; c.cell_field_58=-1; c.tube_field_116=-1;
        assert(stage.install_base_cells(assets.geometry(),std::move(fixture),0,{}));
        auto world=stage.snapshot();
        // The missing prior phases are supplied only for this placement fixture.
        world.setup_progress=SetupProgress::ReadyForInitialUnits;
        sim::TheaterInputs theater; theater.theater_name=assets.theater().theater_name();
        theater.settings=assets.theater().settings();
        for (const auto& set:assets.theater().tile_sets()) theater.tile_sets.push_back({set.first,set.count});
        theater.resolved_metadata=[&](std::uint32_t index,std::uint8_t subtile)->std::optional<sim::TileMetadata> {
            try {
                const auto count=assets.theater().resolved_subtile_count(index);
                if (!count) return {};
                const bool valid=assets.theater().resolved_subtile_present(index,subtile);
                const auto normalized=static_cast<std::uint8_t>(subtile%count);
                if (!assets.theater().resolved_subtile_present(index,normalized)) return sim::TileMetadata{0,0,false};
                const auto& tile=assets.theater().resolved_metadata(index,normalized);
                return sim::TileMetadata{tile.terrain,tile.ramp,valid};
            } catch (const render::RenderError&) { return {}; }
        };
        BaseUnitProfile profile;
        profile.restricted_land=-1; profile.movement_mode=-1; profile.placement_mode=2;
        profile.speed_type=1; profile.level_height=104; profile.leptons_per_sight_increase=2000;
        profile.type_flag_aa0=false; profile.unit_flag_32c=false; profile.linked_team=false;
        profile.type_flag_205=true; profile.unit_flag_32a=false; profile.type_flag_bb0=false; profile.type_flag_bb1=false;
        sim::CellSetupStatic guard; guard.receiver_tile_index=131; guard.animation_index=-1;
        guard.shadow_enabled=false; guard.bounds_height=30;
        const InitialBaseUnitInput input{houses->houses[0].id,"AMCV",1000,0,false,{135,80},{34688,20608,0},profile};
        {
            auto blocked=world;
            auto& cell=blocked.cells.at(80u*512+135);
            cell.terrain->land_type=2; cell.unit_land_speed=0.0f; cell.land_wheel=0.0f;
            cell.coarse_category=4;
            const auto attempted=place_direct_base_unit(blocked,input,guard,theater,0,{{0,1.0f},{2,0.0f}});
            assert(attempted && attempted->placement==PlacementResult::Blocked && blocked.entities.size()==1);
            assert(blocked.entities[0].lifetime==Lifetime::Created && blocked.entities[0].on_map==true);
            const auto saved=blocked.random;
            assert(!search_blocked_base_unit(blocked,attempted->unit_id,{135,80},{1,1,198,198},
                                              profile,guard,theater,0,{{0,1.0f},{2,0.0f}}));
            assert(blocked.random.table==saved.table && blocked.random.first==saved.first &&
                   blocked.random.second==saved.second && blocked.entities[0].lifetime==Lifetime::Created);
        }
        auto wrong_start=input; wrong_start.start_position.z=1;
        assert(!place_direct_base_unit(world,wrong_start,guard,theater,0,{{0,1.0f}}));
        auto missing=input; missing.profile.type_flag_205.reset();
        const auto before=world.random;
        assert(!place_direct_base_unit(world,missing,guard,theater,0,{{0,1.0f}}));
        assert(world.entities.empty() && world.random.table==before.table && world.random.first==before.first &&
               world.random.second==before.second);
        auto no_bounds=guard; no_bounds.bounds_height.reset();
        assert(!place_direct_base_unit(world,input,no_bounds,theater,0,{{0,1.0f}}));
        assert(world.entities.empty() && world.random.table==before.table && world.random.first==before.first);
        const auto placed=place_direct_base_unit(world,input,guard,theater,0,{{0,1.0f}});
        assert(placed && placed->placement==PlacementResult::Placed && !world.pending_cell_setup);
        assert(world.entities.size()==1 && world.entities[0].id==placed->unit_id &&
               world.entities[0].maximum_hp==1000 && world.entities[0].position->z==0 &&
               world.entities[0].hull_facing==0 && world.entities[0].height_byte==0);
        assert(world.cells.at(80u*512+135).heads->ground_head==placed->unit_id &&
               world.cells.at(80u*512+135).ground_occupancy_124==0x20);
        sim::Random expected(before); expected.next();
        assert(world.random.table==expected.state().table && world.random.first==expected.first() &&
               world.random.second==expected.second());
    }
    {
        PrototypeBattleSetup options;
        options.players={{"Player","Americans",5000,0,0,2,true},
                         {"Enemy","Russians",5000,1,1,2,false}};
        const auto initial=sim::Random(42).state();
        const auto battle=prototype_battle(assets,options,initial);
        assert(battle && battle->snapshot.setup_progress==SetupProgress::PrototypeComplete);
        const auto& world=battle->snapshot;
        assert(validate(world) && world.geometry && world.cell_order && world.cell_order->size()==19900);
        assert(world.houses.size()>=4 && world.houses[0].human_controlled==true &&
               world.houses[1].human_controlled==false && world.houses[0].cash==5000);
        const auto count=[&](const std::string& type,std::uint32_t house) {
            return std::count_if(world.entities.begin(),world.entities.end(),[&](const auto& entity) {
                return entity.type==type && entity.house_id==house && entity.lifetime==Lifetime::Placed;
            });
        };
        assert(count("AMCV",world.houses[0].id)==1 && count("MTNK",world.houses[0].id)==2);
        assert(count("SMCV",world.houses[1].id)==1 && count("HTNK",world.houses[1].id)==2);
        assert(std::count_if(world.entities.begin(),world.entities.end(),[](const auto& entity){
            return entity.kind==Kind::Terrain;
        })==206);
        assert(std::count_if(world.entities.begin(),world.entities.end(),[](const auto& entity){
            return entity.kind==Kind::Structure;
        })==25);
        assert(world.entities.size()==206+25+5+6);
        assert(world.houses[0].side_index==0 && world.houses[1].side_index==1);
        const auto player=std::find_if(world.entities.begin(),world.entities.end(),[&](const auto& entity){
            return entity.type=="AMCV" && entity.house_id==world.houses[0].id;
        });
        assert(player!=world.entities.end() && player->cell && player->cell->x==135 && player->cell->y==80 &&
               player->position && player->position->z==416 && player->resolved_hp==1000 && player->next &&
               player->hull_facing==128 && player->hull_facing_raw==0x8000);
        const auto& start=world.cells.at(80u*512+135);
        assert(start.heads && start.heads->ground_head==player->id && start.ground_occupancy_124==0x20);
        for (const auto& entity:world.entities) if (entity.lifetime==Lifetime::Placed) {
            assert(entity.cell && entity.position && entity.next && entity.layer==Layer::Ground);
            const auto& cell=world.cells.at(std::uint32_t(entity.cell->y)*512+entity.cell->x);
            assert(cell.heads && cell.ground_occupancy_124 && (*cell.ground_occupancy_124&0x20));
        }
        for (const auto& entity:world.entities) if (entity.type=="AMCV" || entity.type=="SMCV") {
            if (entity.house_id!=world.houses[0].id && entity.house_id!=world.houses[1].id) continue;
            assert(entity.cell && entity.hull_facing==128 && entity.hull_facing_raw==0x8000 &&
                   entity.object_field_74==1 && entity.object_field_75==1 && entity.object_field_76==0);
            for (int dy=-1;dy<=2;++dy) for (int dx=-1;dx<=2;++dx) {
                if (!dx && !dy) continue;
                const auto key=std::uint32_t(int(entity.cell->y)+dy)*512+int(entity.cell->x)+dx;
                const auto& cell=world.cells.at(key);
                assert(cell.ground_occupancy_124==0 && cell.heads && cell.heads->ground_head==0);
            }
        }
        const auto repeated=prototype_battle(assets,options,initial);
        assert(repeated && repeated->snapshot.random.table==world.random.table &&
               repeated->snapshot.random.first==world.random.first && repeated->snapshot.random.second==world.random.second);
        options.players[1].waypoint_index=0;
        assert(!prototype_battle(assets,options,initial));
        assert(initial.table==sim::Random(42).state().table && initial.first==0 && initial.second==103);
        std::cout<<"MP02T2 prototype has two houses, six starting vehicles, and linked map actors\n";
    }
    auto moved=std::move(assets);
    assert(moved.theater().resolved_metadata(131,0).present);
    assert(moved.cells_in_original_order().size()==19900);
    const sim::Random initial(42);
    sim::Random expected(initial.state());
    const auto state=construct_house_fragments(initial.state(),
        {{"Player","Americans",5000,0,0},{"Enemy","Russians",5000,1,1}},55,7,2,3);
    assert(state && validate(*state) && state->houses.size()==4 && state->creation_order.size()==4);
    for (const auto& house:state->houses) {
        assert(house.constructor_random==expected.ranged(450,1800));
        assert(!house.constructor_complete);
    }
    assert(state->random.table==expected.state().table && state->random.first==expected.first() &&
           state->random.second==expected.second());
    assert(state->houses[0].country=="Americans" && state->houses[0].cash==5000 &&
           state->houses[0].palette_color==session::multiplayer_house_color(0));
    assert(state->houses[2].role==HouseRole::Neutral && state->houses[3].role==HouseRole::Special);
    assert(state->houses[2].palette_color==7 && state->cells.empty() && state->entities.empty());
    const auto unknown_palette=construct_house_fragments(initial.state(),{{"P","Americans",5000,0,{}}},55,{});
    assert(unknown_palette && !unknown_palette->houses[1].palette_color);
    assert(!construct_house_fragments(initial.state(),{},55,7));
    assert(!construct_house_fragments(initial.state(),{{"P","Americans",5000,8,{}}},55,7));
    auto invalid=initial.state(); invalid.second=invalid.first;
    assert(!construct_house_fragments(invalid,{{"P","Americans",5000,0,{}}},55,7));
    bool missing=false;
    try { (void)BaseBattleAssets::load(argv[1],"missing-battle-map.map"); }
    catch (const std::runtime_error&) { missing=true; }
    assert(missing);
    std::cout<<"Base Battle assets and house constructor boundary passed; later phases remain unresolved\n";
}
