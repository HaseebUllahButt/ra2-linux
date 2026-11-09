#include "live_cell_setup.h"
#include "test_cell_setup_inputs.h"
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(ra2map::Cell c) { return std::uint32_t(c.y)*512+c.x; }
}
int main() try {
    for (unsigned seed=0;seed<512;++seed) {
        test::CellSetupFixture f(seed);
        const auto expected=original_overlay_free_cell_setup(f.state,f.guard,f.theater,f.cliff,f.callbacks);
        require(expected.status==CellSetupStatus::Complete,"fixture original setup failed");
        sw::Snapshot world;world.setup_progress=sw::SetupProgress::Complete;world.random=Random(37).state();
        sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
        sw::Entity e;e.id=2;e.house_id=1;e.type="T";e.lifetime=sw::Lifetime::Placed;e.cell=f.state.terrain.cell;
        e.position=ground_cell_center(20,20);e.layer=sw::Layer::Ground;e.next=0;
        CoarseOccupantState kind;kind.runtime_type=1;e.category=kind;world.entities.push_back(e);
        world.creation_order={{sw::CreatedKind::House,1},{sw::CreatedKind::Entity,2}};
        auto& c=world.cells[key(f.state.terrain.cell)];c.allocated=true;c.terrain=f.state.terrain;
        c.cell_field_11d=f.state.field_11d;c.cell_flag_20000=f.state.animated_20000;c.usable=f.usable;
        c.cell_flag_100=f.state.terrain.bridge;c.heads=weapons::CellContentHeads{2,0};
        c.cell_flags_140=(*c.cell_flag_100?0x100u:0u)|(*c.cell_flag_20000?0x20000u:0u);
        std::vector<bool> allocation(512*512);allocation[key(f.state.terrain.cell)]=true;
        for (const auto& item:f.neighbors) {
            const auto index=key(item.second.cell);auto& other=world.cells[index];other.allocated=true;other.terrain=item.second;
            allocation[index]=true;
        }
        LiveCellSetupInputs input;input.theater=&f.theater;input.cliff=f.cliff;input.allocated=&allocation;
        input.fallback=original_default_terrain_cell({0,0});
        input.guard=[&](std::uint32_t,std::uint32_t tile)->std::optional<CellSetupStatic>{
            if (tile!=*f.guard.receiver_tile_index) return {};
            return f.guard;
        };
        for (int land=0;land<32;++land) input.land_wheels[land]=(seed+land)%3?1.0f:0.0f;
        const auto before=world.random;
        auto missing_allocation=allocation;missing_allocation[key({20,19})]=false;
        auto fallback_input=input;fallback_input.allocated=&missing_allocation;
        require(original_live_cell_setup(world,key(f.state.terrain.cell),fallback_input).status==CellSetupStatus::NeedsInput,
                "unrepresented fallback coordinate write was accepted");
        auto missing=input;missing.guard={};
        require(original_live_cell_setup(world,key(f.state.terrain.cell),missing).status==CellSetupStatus::NeedsInput,
                "unresolved TileType guard was accepted");
        require(world.random.table==before.table && world.cells.at(key(f.state.terrain.cell)).terrain->resolved_tile_index==f.state.terrain.resolved_tile_index,
                "failed Cell setup published changes");
        const auto actual=original_live_cell_setup(world,key(f.state.terrain.cell),input);
        if (actual.status!=CellSetupStatus::Complete) std::cerr<<seed<<' '<<actual.detail<<'\n';
        require(actual.status==CellSetupStatus::Complete && f.digest(actual)==f.digest(expected),"live Cell setup differs from recovered body");
        const auto& after=world.cells.at(key(f.state.terrain.cell));
        require(after.coarse_category==expected.category->category && after.coarse_category_writes==expected.category->writes &&
                after.coarse_cache_height==expected.cache_height && after.hierarchy_cache_height==expected.cache_height &&
                after.heads->ground_head==2 && world.entities[0].next==0 && world.random.table==before.table &&
                world.random.first==before.first && world.random.second==before.second,
                "live Cell setup did not preserve ordered links, output, or RNG");
    }
    sw::Snapshot alias_world;std::vector<bool> allocation(512*512);
    auto& alias=alias_world.cells[511];alias.allocated=true;alias.terrain=original_default_terrain_cell({511,0});allocation[511]=true;
    const auto aliased=original_live_cell_neighbor(alias_world,allocation,original_default_terrain_cell({0,0}),{0xffff,1});
    require(aliased && aliased->allocated && aliased->terrain.cell.x==511 && aliased->terrain.cell.y==0,
            "signed (-1,1) did not alias Cell (511,0)");
    const auto negative=original_live_cell_neighbor(alias_world,allocation,original_default_terrain_cell({0,0}),{0xffff,0});
    require(negative && !negative->allocated,"signed (-1,0) did not use the known fallback");
    alias_world.cells.erase(511);
    require(!original_live_cell_neighbor(alias_world,allocation,original_default_terrain_cell({0,0}),{0xffff,1}),"unknown aliased Cell was treated as fallback");
    allocation[511]=false;
    require(!original_live_cell_neighbor(alias_world,allocation,{}, {0xffff,1}),"unknown fallback metadata was accepted");
    const auto null_alias=original_live_cell_neighbor(alias_world,allocation,original_default_terrain_cell({0,0}),{0xffff,1});
    require(null_alias && !null_alias->allocated,"known null alias did not use fallback");
    std::cout<<"512 checked Cell setup bindings, signed aliases, and atomic rejection passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
