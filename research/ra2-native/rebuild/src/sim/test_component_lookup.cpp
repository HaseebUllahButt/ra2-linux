#include "test_component_lookup_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    if (argc>2 || (argc==2 && std::string(argv[1])!="--vectors" && std::string(argv[1])!="--transition-vectors"))
        throw std::runtime_error("usage: test_component_lookup [--vectors|--transition-vectors]");
    const bool vectors=argc==2 && std::string(argv[1])=="--vectors";
    std::uint32_t transition_hash=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        TransitionFixture f(seed);const auto r=original_find_component_transition(f.point,f.tolerance,f.start,f.records);
        require(r.status==ComponentLookupStatus::Complete,"resolved transition lookup did not complete");
        const auto v=static_cast<std::uint32_t>(*r.index);transition_hash=(transition_hash^v)*16777619u;
        if (argc==2 && !vectors) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<v<<std::dec<<"\n";
    }
    require(transition_hash==0xc02faa0cu,"original transition selection vectors differ");
    if (argc==1) std::cout<<"4096 transition selections "<<std::hex<<transition_hash<<std::dec<<"\n";
    if (argc==2 && !vectors) return 0;
    for (auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) {
        std::uint32_t total=2166136261u;
        for (unsigned seed=0;seed<2048;++seed) {
            ComponentFixture f(seed,edition);
            const auto r=original_movement_component(f.graph,f.movement,f.stride,f.query,f.input,f.lookup());
            if (r.status!=ComponentLookupStatus::Complete) throw std::runtime_error("component fixture "+std::to_string(seed)+": "+r.detail);
            const auto h=f.digest(r);total=(total^h)*16777619u;
            if (vectors) std::cout<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
        }
        require(total==(edition==DriveEdition::BaseRa2?0x6f81653du:0x9b3d78eau),"original bridge component vectors differ");
        if (argc==1) std::cout<<"2048 complete components "<<(edition==DriveEdition::BaseRa2?"base ":"yuri ")<<std::hex<<total<<std::dec<<"\n";
    }
    unsigned checks=0;
    const auto transition_fail=[&](std::vector<ComponentTransition> records,ComponentLookupStatus expected,int start=0) {
        const auto r=original_find_component_transition({4,4},1,start,records);
        require(r.status==expected && !r.index && !r.detail.empty(),"unknown transition state became an index");++checks;
    };
    transition_fail({{}},ComponentLookupStatus::NeedsInput);
    transition_fail({{std::nullopt,std::nullopt,std::nullopt,0}},ComponentLookupStatus::NeedsInput);
    transition_fail({{ra2map::Cell{4,3},std::nullopt,std::nullopt,0}},ComponentLookupStatus::NeedsInput);
    transition_fail({},ComponentLookupStatus::Unsupported,-1);
    auto tr=original_find_component_transition({4,4},1,0,{{std::nullopt,std::nullopt,std::nullopt,-1}});
    require(tr.status==ComponentLookupStatus::Complete && tr.index==-1,"skipped transition required endpoint data");++checks;
    tr=original_find_component_transition({4,4},1,0,{{ra2map::Cell{4,3},ra2map::Cell{4,6},std::nullopt,0}});
    require(tr.status==ComponentLookupStatus::Complete && tr.index==0,"transition selection used field +8");++checks;
    tr=original_find_component_transition({4,4},1,1,{{}});
    require(tr.status==ComponentLookupStatus::Complete && tr.index==-1,"empty transition suffix read unknown earlier data");++checks;
    ComponentFixture f(4,DriveEdition::BaseRa2);
    const auto fail=[&](MovementComponentQuery q,ComponentLookupInputs in,ComponentCellLookup lookup,ComponentLookupStatus expected) {
        const auto r=original_movement_component(f.graph,f.movement,f.stride,q,in,lookup);
        require(r.status==expected && !r.component && !r.detail.empty(),"unresolved component state published a component");++checks;
    };
    fail(f.query,f.input,{},ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[](ra2map::Cell)->std::optional<ComponentCellState>{return {};},ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[](ra2map::Cell){return ComponentCellState{};},ComponentLookupStatus::NeedsInput);
    auto in=f.input;in.transitions.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    in=f.input;in.transitions->at(0).field_c.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    in=f.input;in.transitions->at(0).first.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    in=f.input;in.transitions->at(0).second.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    in=f.input;in.transitions->at(0).field_8.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[&](ra2map::Cell p){auto c=f.metadata(p);c.object_cell.reset();return c;},ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[&](ra2map::Cell p)->std::optional<ComponentCellState>{if (component_key(p)!=component_key(f.query.cell)) return {};return f.metadata(p);},ComponentLookupStatus::NeedsInput);
    in=f.input;in.first_tile_range.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    in=f.input;in.first_tile_range=100;in.second_tile_range.reset();fail(f.query,in,f.lookup(),ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[&](ra2map::Cell p){auto c=f.metadata(p);c.tile_index.reset();return c;},ComponentLookupStatus::NeedsInput);
    fail(f.query,f.input,[&](ra2map::Cell p){auto c=f.metadata(p);c.land_type.reset();return c;},ComponentLookupStatus::NeedsInput);
    auto q=f.query;q.maximum_walk_steps=0;fail(q,f.input,f.lookup(),ComponentLookupStatus::Unsupported);
    fail(f.query,f.input,[&](ra2map::Cell){return ComponentCellState{f.query.cell,true,0,0};},ComponentLookupStatus::Unsupported);
    q=f.query;q.above_bridge=false;
    auto r=original_movement_component(f.graph,f.movement,f.stride,q,{},{});
    require(r.status==ComponentLookupStatus::Complete && r.steps==std::vector<ComponentLookupStep>{ComponentLookupStep::Ground},"ground query read bridge inputs");++checks;
    q.movement_zone=99;fail(q,{}, {},ComponentLookupStatus::Unsupported);
    r=original_movement_component(f.graph,f.movement,f.stride,f.query,{},[](ra2map::Cell){return ComponentCellState{std::nullopt,false,std::nullopt,std::nullopt};});
    require(r.status==ComponentLookupStatus::Complete && r.selected_cell && component_key(*r.selected_cell)==component_key(f.query.cell),"unmarked source required bridge records or object coordinate");++checks;
    ComponentFixture no_match(2,DriveEdition::BaseRa2);
    r=original_movement_component({}, {},0,no_match.query,no_match.input,no_match.lookup());
    require(r.status==ComponentLookupStatus::Complete && r.component==0xffffffffu && !r.selected_cell && r.transition_index==-1,"bridge miss read ground tables or became FFFF");++checks;
    auto dummy=f.graph;dummy.labels.assign(dummy.labels.size(),0);q=f.query;q.above_bridge=false;
    r=original_movement_component(dummy,f.movement,f.stride,q,{},{});
    require(r.status==ComponentLookupStatus::Complete && r.component==0xffffu,"ground FFFF word became bridge FFFFFFFF");++checks;
    for (auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) {
        ComponentFixture range(8,edition);range.input.first_tile_range=-1;range.input.second_tile_range=-1;
        r=original_movement_component(range.graph,range.movement,range.stride,range.query,range.input,[&](ra2map::Cell p){auto c=range.metadata(p);c.tile_index.reset();return c;});
        require(edition==DriveEdition::BaseRa2?(r.status==ComponentLookupStatus::NeedsInput && !r.component):r.status==ComponentLookupStatus::Complete,"edition-specific -1 range guard differs");++checks;
        require(original_component_tile_range(edition,-1,0)==(edition==DriveEdition::BaseRa2),"raw -1 tile-range decision differs");++checks;
        require(original_component_tile_range(edition,100,100) && original_component_tile_range(edition,100,115) &&
            !original_component_tile_range(edition,100,116) && !original_component_tile_range(edition,0x7ffffff8,0x7ffffffc),"signed tile-range bounds or wrapped upper limit differ");++checks;
    }
    ComponentFixture not_range(6,DriveEdition::BaseRa2);
    r=original_movement_component(not_range.graph,not_range.movement,not_range.stride,not_range.query,not_range.input,[&](ra2map::Cell p){auto c=not_range.metadata(p);c.land_type.reset();return c;});
    require(r.status==ComponentLookupStatus::Complete,"unmatched tile ranges required LandType");++checks;
    if (argc==1) std::cout<<"8192 original transition and component vectors and "<<checks<<" input checks pass\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<"\n";return 1;}
