#include "world.h"
#include "sim/test_cell_setup_inputs.h"
#include <cassert>
#include <iostream>
#include <iomanip>

using namespace ra2;
using namespace ra2::session::world;

int main(int argc,char** argv) {
    const bool vectors=argc==2 && std::string(argv[1])=="--vectors";
    if (argc!=1 && !vectors) return 2;
    std::uint32_t hash=2166136261u;
    bool checked_order=false;
    for (unsigned seed=0;seed<4096;++seed) {
        sim::test::CellSetupFixture fixture(seed);
        Snapshot snapshot;
        snapshot.random=sim::Random(42).state();
        const auto key=20u*512+20;
        snapshot.pending_cell_setup=true; snapshot.pending_setup_cell=key;
        auto& cell=snapshot.cells[key];
        cell.allocated=true;
        cell.terrain=fixture.state.terrain;
        cell.cell_field_11d=fixture.state.field_11d;
        cell.cell_flag_20000=fixture.state.animated_20000;
        cell.usable=fixture.usable;
        cell.heads=weapons::CellContentHeads{};
        if (fixture.unit) {
            House house; house.id=1; house.name="Player"; house.country="Americans";
            Entity unit; unit.id=2; unit.house_id=1; unit.type="AMCV";
            unit.lifetime=Lifetime::Placed; unit.position=sim::Coord{0,0,0};
            unit.cell=ra2map::Cell{20,20}; unit.layer=Layer::Ground; unit.next=0;
            unit.category.emplace(); unit.category->runtime_type=1;
            snapshot.houses.push_back(house); snapshot.entities.push_back(unit);
            snapshot.creation_order={{CreatedKind::House,1},{CreatedKind::Entity,2}};
            cell.heads->ground_head=2;
        }
        for (const auto& item:fixture.neighbors) {
            const auto& terrain=item.second;
            auto& neighbor=snapshot.cells[std::uint32_t(terrain.cell.y)*512+terrain.cell.x];
            neighbor.allocated=true; neighbor.terrain=terrain;
        }
        std::map<std::int32_t,float> tracks;
        for (int land=0;land<16;++land) tracks[land]=(seed+land)%3?1.0f:0.0f;
        Stage stage(snapshot);
        const auto result=stage.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks);
        assert(result.status==sim::CellSetupStatus::Complete);
        const auto& complete=stage.snapshot();
        const auto& actual=complete.cells.at(key);
        assert(!complete.pending_cell_setup && !complete.pending_setup_cell);
        assert(actual.terrain && actual.cell_field_11d && actual.coarse_category &&
               actual.coarse_category_writes && actual.coarse_cache_height && actual.hierarchy_cache_height);
        assert(*actual.coarse_cache_height==*result.cache_height &&
               *actual.hierarchy_cache_height==*result.cache_height);
        assert(actual.terrain->bridge==fixture.state.terrain.bridge);
        if (actual.terrain->land_type) assert(actual.land_track==tracks.at(*actual.terrain->land_type));
        assert(complete.random.table==snapshot.random.table && complete.random.first==snapshot.random.first &&
               complete.random.second==snapshot.random.second && complete.entities.size()==snapshot.entities.size());
        sim::CellSetupResult committed;
        committed.state.terrain=*actual.terrain;
        committed.state.field_11d=actual.cell_field_11d;
        committed.state.animated_20000=actual.cell_flag_20000;
        committed.category=sim::CoarseCategoryResult{*actual.coarse_category,*actual.coarse_category_writes};
        committed.cache_height=actual.coarse_cache_height;
        const auto digest=fixture.digest(committed);
        hash=(hash^digest)*16777619u;
        if (vectors) std::cout<<seed<<' '<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<'\n';
        if (!checked_order && fixture.usable && actual.terrain->land_type &&
            *actual.terrain->land_type!=2 && *actual.terrain->land_type!=6 &&
            tracks.at(*actual.terrain->land_type)>0.01f) {
            auto ordered=snapshot; ordered.session_field_838=0;
            Entity first; first.id=3; first.kind=Kind::Terrain; first.type="TREE1";
            first.lifetime=Lifetime::Placed; first.position=sim::Coord{0,0,0};
            first.cell=ra2map::Cell{20,20}; first.layer=Layer::Ground; first.next=4;
            first.category.emplace(); first.category->runtime_type=36; first.category->terrain_field_2a8=7;
            Entity second=first; second.id=4; second.type="TREE2";
            second.next=ordered.cells.at(key).heads->ground_head; second.category->terrain_field_2a8=0;
            ordered.entities.push_back(first); ordered.entities.push_back(second);
            ordered.creation_order.push_back({CreatedKind::Entity,3});
            ordered.creation_order.push_back({CreatedKind::Entity,4});
            ordered.cells.at(key).heads->ground_head=3;
            Stage first_stage(ordered);
            assert(first_stage.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks).status==sim::CellSetupStatus::Complete);
            assert(first_stage.snapshot().cells.at(key).coarse_category==2);
            ordered.entities[ordered.entities.size()-2].next=second.next;
            ordered.entities.back().next=3;
            ordered.cells.at(key).heads->ground_head=4;
            Stage second_stage(ordered);
            assert(second_stage.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks).status==sim::CellSetupStatus::Complete);
            assert(second_stage.snapshot().cells.at(key).coarse_category==5);
            checked_order=true;
        }
        if (seed==3) {
            auto missing=fixture.guard; missing.bounds_height.reset();
            Stage rejected(snapshot);
            const auto failed=rejected.complete_pending_cell_setup(missing,fixture.theater,fixture.cliff,tracks);
            assert(failed.status==sim::CellSetupStatus::NeedsInput);
            assert(rejected.snapshot().pending_cell_setup && rejected.snapshot().pending_setup_cell==key);
            assert(rejected.snapshot().random.table==snapshot.random.table &&
                   rejected.snapshot().random.first==snapshot.random.first &&
                   rejected.snapshot().random.second==snapshot.random.second);
            assert(rejected.snapshot().cells.at(key).cell_field_11d==cell.cell_field_11d &&
                   !rejected.snapshot().cells.at(key).coarse_category);
            assert(rejected.snapshot().entities.size()==snapshot.entities.size());
            auto unsupported=fixture.guard; unsupported.receiver_tile_index=999;
            const auto wrong_type=rejected.complete_pending_cell_setup(unsupported,fixture.theater,fixture.cliff,tracks);
            assert(wrong_type.status==sim::CellSetupStatus::Unsupported);
            assert(rejected.snapshot().pending_cell_setup && rejected.snapshot().random.table==snapshot.random.table);
            auto no_neighbor=snapshot;
            no_neighbor.cells.erase(19u*512+20);
            Stage unknown_neighbor(no_neighbor);
            const auto no_cell=unknown_neighbor.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks);
            assert(no_cell.status==sim::CellSetupStatus::NeedsInput && unknown_neighbor.snapshot().pending_cell_setup);
            auto no_allocation=snapshot;
            no_allocation.cells.at(key).allocated.reset();
            Stage unknown_allocation(no_allocation);
            const auto no_receiver=unknown_allocation.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks);
            assert(no_receiver.status==sim::CellSetupStatus::NeedsInput && unknown_allocation.snapshot().pending_cell_setup);
            auto no_bridge=snapshot;
            no_bridge.cells.at(key).terrain->bridge.reset();
            Stage unknown_bridge(no_bridge);
            const auto bridge_result=unknown_bridge.complete_pending_cell_setup(fixture.guard,fixture.theater,fixture.cliff,tracks);
            assert(bridge_result.status==sim::CellSetupStatus::Complete &&
                   !unknown_bridge.snapshot().cells.at(key).terrain->bridge);
        }
    }
    assert(hash==0x0a4940c2u); // Both original editions: original_cell_setup_check.py.
    assert(checked_order);
    if (!vectors) std::cout<<"4096 staged Cell setups match the original checked slice\n";
}
