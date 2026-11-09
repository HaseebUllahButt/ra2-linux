#include "../placement_terrain.h"
#include "sim/test_inputs.h"
#include "sim/world_init.h"
#include "render/minimap.h"
#include "world.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>

// Conditional native fixture for MP02T2. Packed Overlay indexes are treated
// as admitted. The two map passes do not execute Overlay effects, Cell +11D,
// Tube or animation creation, shadow writes, object loading, or bridge setup.
int main(int argc,char** argv) {
    if (argc!=2 && !(argc==3 && (std::string(argv[2])=="--bounds" || std::string(argv[2])=="--stage"))) return 2;
    using namespace ra2;
    const std::string game=argv[1];
    fs::MixFileSystem files(game);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount_overlays();
    files.mount_resources();
    rules::IniFile map,rules;
    map.parse(original_test_ini(game,"MP02T2.MAP",true));
    rules.parse(original_test_ini(game,"RULES.INI",true));
    auto art=render::TheaterArt::load(files,map.read_string("Map","Theater"));
    const auto& record=art.resolved_metadata(131,0);
    assert(record.present && art.resolved_subtile_present(131,0));
    if (argc==3 && std::string(argv[2])=="--bounds") {
        std::cout<<art.tile_width()<<' '<<art.tile_height()<<' '<<record.flags<<' '
                 <<record.y<<' '<<record.extra_y<<'\n';
        return 0;
    }
    // 0052B690 reads the extra-image bit before using the two Y fields.
    assert(art.tile_height()==30 && (record.flags&1u)==0);
    const auto rectangle=[&](const char* name) {
        const auto fields=map.read_list("Map",name);
        assert(fields.size()==4);
        std::int32_t value[4];
        for (int i=0;i<4;++i) {
            const auto parsed=rules::IniFile::integer_value(fields[i]);
            assert(parsed); value[i]=*parsed;
        }
        return session::MapRectangle{value[0],value[1],value[2],value[3]};
    };
    const auto geometry=session::resolve_placement_geometry(rectangle("Size"),rectangle("LocalSize"));
    assert(geometry);
    const auto input=original_test_cells(game,"MP02T2.MAP",true);
    std::map<std::pair<int,int>,const OriginalTestCell*> by_cell;
    for (const auto& cell:input) by_cell[{cell.x,cell.y}]=&cell;
    sim::TerrainCells cells;
    for (const auto coord:sim::original_map_cell_order(geometry->width,geometry->height)) {
        auto cell=sim::original_default_terrain_cell(coord);
        const auto found=by_cell.find({coord.x,coord.y});
        if (found!=by_cell.end()) {
            const auto& src=*found->second;
            cell.tile_index=src.tile; cell.resolved_tile_index=src.resolved;
            cell.subtile=src.subtile; cell.tmp_terrain=src.raw; cell.ramp_index=src.ramp;
            cell.height_level=src.height<128?int(src.height):int(src.height)-256;
            cell.overlay_type_index=src.overlay;
        }
        cell.bridge.reset(); // The omitted bridge phase is unresolved.
        cells.push_back(cell);
    }
    const auto source=original_test_catalog(game,"MP02T2.MAP",true);
    sim::TheaterInputs theater;
    theater.theater_name=source.name; theater.settings.parse(source.settings);
    for (const auto range:source.ranges) theater.tile_sets.push_back({range.first,range.count});
    theater.resolved_metadata=[&](std::uint32_t index,std::uint8_t subtile)->std::optional<sim::TileMetadata> {
        const auto data=source.metadata(index,subtile);
        if (!data) return {};
        return sim::TileMetadata{data->terrain,data->ramp,data->valid};
    };
    const auto cliff=static_cast<std::uint8_t>(map.read_integer("General","CliffBackImpassability",
        rules.read_integer("General","CliffBackImpassability",0)));
    for (int pass=0;pass<2;++pass) {
        sim::initialize_original_tile_pass(cells,theater,geometry->width,geometry->height,cliff,pass==0);
        bool found=false;
        for (const auto& cell:cells) if (cell.cell.x==135 && cell.cell.y==80) {
            assert(cell.resolved_tile_index==131 && cell.tmp_terrain==13 && cell.ramp_index==0);
            assert(cell.land_type==0 && cell.height_level==4 && cell.overlay_type_index==-1);
            assert(!cell.bridge);
            found=true;
        }
        assert(found);
    }
    if (argc==3) {
        using namespace session::world;
        // These runtime values are test inputs, not conclusions about the map.
        // Overlay effects, object loading, bridge setup, Tube and animation
        // creation, and shadow writes were not executed by the two passes.
        assert(map.key_count("Clear")==0 && map.key_count("Tiberium")==0);
        const auto clear_wheel=rules::IniFile::double_value(rules.read_string("Clear","Wheel"));
        const auto clear_track=rules::IniFile::double_value(rules.read_string("Clear","Track"));
        const auto tiberium_wheel=rules::IniFile::double_value(rules.read_string("Tiberium","Wheel"));
        const auto tiberium_track=rules::IniFile::double_value(rules.read_string("Tiberium","Track"));
        assert(clear_wheel && clear_track && tiberium_wheel && tiberium_track && *clear_wheel==1.0 && *clear_track==1.0 &&
               static_cast<float>(*tiberium_wheel)==0.5f && static_cast<float>(*tiberium_track)==0.7f);
        Snapshot category_case; category_case.random=sim::Random(1).state();
        auto& category_cell=category_case.cells[0];
        category_cell.terrain.emplace(); category_cell.terrain->overlay_type_index=-1;
        category_cell.terrain->land_type=5; category_cell.usable=true;
        category_cell.heads=weapons::CellContentHeads{};
        category_cell.land_wheel=static_cast<float>(*tiberium_wheel);
        category_cell.unit_land_speed=static_cast<float>(*tiberium_track); // Supplied SpeedType 1.
        const auto category_profile=coarse_inputs(category_case,0);
        assert(category_profile && category_profile->land_wheel==0.5f &&
               category_cell.unit_land_speed==0.7f && sim::original_coarse_category(*category_profile).category==0);
        category_cell.land_wheel=0.0f;
        assert(sim::original_coarse_category(*coarse_inputs(category_case,0)).category==6);
        category_cell.land_wheel=0.5f; category_cell.unit_land_speed=0.0f;
        assert(sim::original_coarse_category(*coarse_inputs(category_case,0)).category==0);
        Stage stage(sim::Random(42).state());
        const auto house=stage.create_player_house("Player","Americans",10000,0,64);
        assert(house);
        const auto unit=stage.create_unit(*house,"AMCV",1000,0,false);
        assert(unit);
        std::map<std::uint32_t,CellState> staged_cells;
        for (const auto& terrain:cells) {
            const int dx=int(terrain.cell.x)-135,dy=int(terrain.cell.y)-80;
            if ((dx>=-1 && dx<=1 && dy>=-1 && dy<=1) || (dx==2 && dy==2)) {
                auto& state=staged_cells[std::uint32_t(terrain.cell.y)*512+terrain.cell.x];
                state.allocated=true; state.terrain=terrain; state.neighbor_count=0;
            }
        }
        constexpr std::uint32_t target=80u*512+135;
        assert(staged_cells.size()==10 && staged_cells.count(target));
        assert(stage.install_base_cells(*geometry,std::move(staged_cells),{},{}));
        BaseUnitProfile profile;
        profile.restricted_land=-1; profile.movement_mode=-1; profile.placement_mode=2;
        profile.speed_type=1; profile.level_height=104; profile.type_flag_aa0=false;
        profile.unit_flag_32c=false; profile.linked_team=false; profile.type_flag_205=true;
        profile.unit_flag_32a=false; profile.type_flag_bb0=false; profile.type_flag_bb1=false;
        assert(stage.place_base_unit(*unit,{135,80},32,profile)==PlacementResult::Unresolved);
        auto supplied=stage.snapshot();
        supplied.tube_vector_count=0;
        auto& target_cell=supplied.cells.at(target);
        target_cell.heads=weapons::CellContentHeads{};
        target_cell.terrain->bridge=false; target_cell.cell_flag_100=false;
        target_cell.ground_occupancy_124=0; target_cell.cell_field_54=-1;
        target_cell.cell_field_58=-1; target_cell.tube_field_116=-1;
        // The original constructor and AMCV reader fragments select Track 1;
        // the omitted type-loading and map phases remain fixture assumptions.
        target_cell.unit_land_speed=static_cast<float>(*clear_track);
        target_cell.land_wheel=static_cast<float>(*clear_wheel);
        target_cell.usable=true; target_cell.coarse_category=0;
        Stage prepared(std::move(supplied));
        assert(prepared.place_base_unit(*unit,{135,80},32,profile)==PlacementResult::Placed);
        const auto before=prepared.snapshot().random;
        sim::CellSetupStatic guard;
        guard.receiver_tile_index=131; guard.animation_index=-1;
        guard.shadow_enabled=false; guard.bounds_height=30;
        auto missing_bounds=guard; missing_bounds.bounds_height.reset();
        const auto incomplete=prepared.complete_pending_cell_setup(missing_bounds,theater,cliff,{{0,static_cast<float>(*clear_wheel)}});
        assert(incomplete.status==sim::CellSetupStatus::NeedsInput);
        assert(prepared.snapshot().pending_cell_setup && prepared.snapshot().random.table==before.table &&
               prepared.snapshot().random.first==before.first && prepared.snapshot().random.second==before.second &&
               prepared.snapshot().cells.at(target).ground_occupancy_124==0x20);
        const auto completed=prepared.complete_pending_cell_setup(guard,theater,cliff,{{0,static_cast<float>(*clear_wheel)}});
        if (completed.status!=sim::CellSetupStatus::Complete) {
            std::cerr<<"conditional stage setup needs input: "<<completed.detail<<'\n';
            return 1;
        }
        const auto& result=prepared.snapshot();
        const auto& after=result.cells.at(target);
        assert(!result.pending_cell_setup && after.cell_field_11d==0 && after.coarse_category==0);
        assert(after.coarse_cache_height==4 && after.hierarchy_cache_height==4);
        assert(after.heads->ground_head==*unit && after.ground_occupancy_124==0x20);
        assert(after.terrain->bridge==false && after.unit_land_speed==1.0f);
        assert(result.entities.at(0).lifetime==Lifetime::Placed && result.entities.at(0).cell &&
               result.entities.at(0).cell->x==135 && result.entities.at(0).cell->y==80);
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) if (dx || dy)
            assert(result.cells.at(std::uint32_t(80+dy)*512+135+dx).neighbor_count==1);
        assert(result.random.table==before.table && result.random.first==before.first && result.random.second==before.second);
        std::cout<<"MP02T2 staged placement and Cell setup pass with supplied runtime values\n";
        return 0;
    }
    std::cout<<"MP02T2 conditional tile passes keep waypoint 0 at tile 131, ramp 0, land 0\n";
}
