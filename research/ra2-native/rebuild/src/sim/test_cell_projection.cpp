#include "test_cell_projection_inputs.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
using namespace ra2::sim::test;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
int main(int argc,char** argv) try {
    if (argc>2 || (argc==2 && std::string(argv[1])!="--vectors")) throw std::runtime_error("usage: test_cell_projection [--vectors]");
    std::uint32_t total=2166136261u;
    for (unsigned seed=0;seed<4096;++seed) {
        ProjectionFixture fixture(seed);
        const auto result=original_cell_projection(fixture.point,fixture.callback());
        if (result.status!=CellProjectionStatus::Complete) throw std::runtime_error("projection fixture "+std::to_string(seed)+": "+result.detail);
        const auto digest=fixture.digest(result);total=(total^digest)*16777619u;
        if (argc==2) std::cout<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<digest<<std::dec<<"\n";
    }
    require(total==0x8e108462u,"original projection coordinates, signed heights, flag branches, or lookup order differ");
    if (argc==1) std::cout<<"cell projection digest "<<std::hex<<total<<std::dec<<"\n";
    unsigned checks=0;
    const auto rejected=[&](const ProjectionLookup& lookup,CellProjectionStatus status) {
        const auto result=original_cell_projection({128,128,0},lookup);
        require(result.status==status && !result.answer && !result.detail.empty(),"projection error published an answer or lost its cause");++checks;
    };
    rejected({},CellProjectionStatus::NeedsInput);
    rejected([](ra2map::Cell)->std::optional<ProjectionCellState> { return {}; },CellProjectionStatus::NeedsInput);
    rejected([](ra2map::Cell) { return ProjectionCellState{0,std::nullopt,false}; },CellProjectionStatus::NeedsInput);
    rejected([](ra2map::Cell) { return ProjectionCellState{std::nullopt,false,false}; },CellProjectionStatus::NeedsInput);
    rejected([](ra2map::Cell) { return ProjectionCellState{128,false,false}; },CellProjectionStatus::Unsupported);
    rejected([](ra2map::Cell) { return ProjectionCellState{0,true,std::nullopt}; },CellProjectionStatus::NeedsInput);
    const auto flat=original_cell_projection({-1,-257,123},[](ra2map::Cell) { return ProjectionCellState{0,false,std::nullopt}; });
    require(flat.status==CellProjectionStatus::Complete && flat.answer->x==0 && flat.answer->y==0xffff && flat.steps==208,"flat projection changed signed truncation, unused bridge input, or source return");++checks;
    const auto without_z=original_cell_projection({-1,-257,-2147483647-1},[](ra2map::Cell) { return ProjectionCellState{0,false,std::nullopt}; });
    require(without_z.status==CellProjectionStatus::Complete && projection_key(*without_z.answer)==projection_key(*flat.answer) && without_z.lookups==flat.lookups,"projection read input Z");++checks;
    unsigned calls=0;
    const auto partial=original_cell_projection({128,128,0},[&](ra2map::Cell) {
        return ++calls==1?ProjectionCellState{std::nullopt,false,std::nullopt}:ProjectionCellState{0,std::nullopt,std::nullopt};
    });
    require(partial.status==CellProjectionStatus::Complete,"projection required flags or heights outside their original point of use");++checks;
    if (argc==1) std::cout<<"4096 original projection vectors and "<<checks<<" input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n";return 1; }
