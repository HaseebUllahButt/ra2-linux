#include "test_cell_setup_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    const bool trace=argc>1 && std::string(argv[1])=="--trace";
    std::uint32_t hash=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        test::CellSetupFixture fixture(seed);
        const auto out=original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks);
        if (out.status!=CellSetupStatus::Complete) throw std::runtime_error("single-Cell fixture "+std::to_string(seed)+": "+out.detail);
        if (trace && seed>=3 && seed<=7) {
            const auto& cell=out.state.terrain;
            std::cout<<"trace "<<seed<<" input "<<*fixture.state.terrain.resolved_tile_index<<" bounds "<<*fixture.guard.bounds_height<<" output "<<*cell.resolved_tile_index<<" "<<unsigned(cell.subtile)<<" "<<*cell.ramp_index<<" "<<*cell.land_type<<" "<<unsigned(*out.state.field_11d)<<" "<<*cell.bridge<<" "<<*out.state.animated_20000<<" "<<unsigned(out.category->category)<<" "<<unsigned(*out.cache_height)<<"\n";
        }
        const auto digest=fixture.digest(out); hash=(hash^digest)*16777619u;
        if (argc>1 && !trace) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
    }
    if (argc>1) return 0;
    std::cout<<"single-Cell setup digest "<<std::hex<<hash<<std::dec<<"\n";
    require(hash==0x0a4940c2u,"original single-Cell setup differs");
    test::CellSetupFixture fixture(3); const auto input=fixture.state;
    fixture.state.terrain.bridge.reset();
    auto out=original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks);
    require(out.status==CellSetupStatus::Complete && !out.state.terrain.bridge,"single-Cell setup invented a bridge flag");
    fixture.state=input; fixture.guard.bounds_height.reset();
    out=original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks);
    require(out.status==CellSetupStatus::NeedsInput && out.state.field_11d==input.field_11d && out.state.terrain.resolved_tile_index==input.terrain.resolved_tile_index,"missing bounds published partial setup");
    fixture.guard.bounds_height=30; fixture.guard.animation_index=7; fixture.guard.animation_subtile=0; fixture.state.animated_20000=false;
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::Unsupported,"required animation was skipped");
    fixture.guard.animation_index.reset(); fixture.guard.animation_subtile.reset(); fixture.state.animated_20000.reset();
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::NeedsInput,"unknown animation guards were accepted");
    fixture.guard.animation_index=-1; fixture.guard.shadow_enabled=true; fixture.guard.shadow_list_empty=false;
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::Unsupported,"required shadow writes were skipped");
    fixture.guard.shadow_enabled.reset(); fixture.guard.shadow_list_empty.reset();
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::NeedsInput,"unknown shadow guards were accepted");
    fixture.guard.shadow_enabled=false; fixture.guard.receiver_tile_index=77;
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::Unsupported,"effect guards used the wrong TileType");
    fixture.guard.receiver_tile_index=input.terrain.resolved_tile_index;
    fixture.theater.resolved_metadata=[](std::uint32_t,std::uint8_t)->std::optional<TileMetadata> { return TileMetadata{5,0,true}; };
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::Unsupported,"required Tube creation was skipped");
    fixture.state.allocated.reset();
    require(original_overlay_free_cell_setup(fixture.state,fixture.guard,fixture.theater,fixture.cliff,fixture.callbacks).status==CellSetupStatus::NeedsInput,"unknown receiver allocation was accepted");
    test::CellSetupFixture receiver(3); bool new_image_read=false;
    const auto old=receiver.state.terrain.resolved_tile_index.value();
    receiver.theater.resolved_metadata=[&](std::uint32_t index,std::uint8_t)->std::optional<TileMetadata> {
        if (index!=old) { new_image_read=true; return {}; }
        return TileMetadata{test::setup_raw(index),0,true};
    };
    out=original_overlay_free_cell_setup(receiver.state,receiver.guard,receiver.theater,receiver.cliff,receiver.callbacks);
    require(out.status==CellSetupStatus::Complete && out.state.terrain.resolved_tile_index!=old && !new_image_read,"single-Cell setup read the replacement TileType too early");
    receiver.callbacks.category=[](const TerrainCell&)->std::optional<CoarseCategoryInputs> { return {}; };
    out=original_overlay_free_cell_setup(receiver.state,receiver.guard,receiver.theater,receiver.cliff,receiver.callbacks);
    require(out.status==CellSetupStatus::NeedsInput && !out.category && out.state.terrain.resolved_tile_index==old && out.state.field_11d==receiver.state.field_11d,"missing category profile published partial setup");
    std::cout<<"4096 original single-Cell setup vectors and eleven effect or input guards pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
