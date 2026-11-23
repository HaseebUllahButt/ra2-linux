#include "resource_cell.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ra2::sim;
namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
std::uint32_t next(std::uint32_t& state) {
    state=state*1664525u+1013904223u; return state>>16;
}
ResourceOverlay overlay(int index) { return {index,index!=-1 && index!=5}; }
ResourceCellInput fixture(unsigned seed) {
    std::uint32_t r=seed^0x9028ac17u;
    constexpr int choices[]={-1,5,10,11,12,13,20,21,22,23,30};
    auto choice=[&]() { return choices[next(r)%11]; };
    ResourceCellInput in;
    in.cell=overlay(choice()); in.data=static_cast<std::uint8_t>(next(r));
    in.cell_coordinate=20u|(20u<<16); in.fallback_coordinate=0x22221111u;
    const int fallback=choice();
    const int absent=seed%5==0?static_cast<int>((seed/5)%8):-1;
    for (int i=0;i<8;++i) {
        const auto o=choice(); in.neighbor_fallback[i]=(i==absent);
        in.neighbors[i]=overlay(i==absent?fallback:o);
    }
    ResourceType a,b;
    const bool swapped=seed%7==0;
    a.index=swapped?1:0; a.first_overlay=10; a.count=3; a.extra_count=1;
    a.value=seed%9==0?0x70000001:25; a.divisor=1+static_cast<int>(next(r)%13);
    a.first_overlay_address=0x2140000u+10*0x288u;
    b.index=swapped?0:1; b.first_overlay=seed%3==0?12:20; b.count=3; b.extra_count=1;
    b.value=-50; b.divisor=1+static_cast<int>(next(r)%13);
    b.first_overlay_address=0x2140000u+static_cast<unsigned>(*b.first_overlay)*0x288u;
    in.registry=std::vector<ResourceType>{a,b}; in.randomize=(seed%4!=0);
    in.random_disabled=(seed%5==1);
    RandomState random; random.first=seed%250; random.second=(seed*7+13)%250;
    for (auto& word:random.table) { const auto high=next(r); const auto low=next(r); word=(high<<16)|low; }
    in.random=random;
    return in;
}
void output(unsigned seed,const ResourceCellResult& result) {
    require(result.status==ResourceCellStatus::Complete,"resolved fixture failed");
    const auto& r=*result.random;
    std::cout<<seed<<' '<<*result.overlay<<' '<<static_cast<unsigned>(*result.data)<<' '<<*result.value
             <<' '<<result.overlay_written<<' '<<result.data_written<<' '<<*result.fallback_coordinate
             <<' '<<result.fallback_writes.size();
    for (const auto& w:result.fallback_writes) std::cout<<' '<<w.neighbor<<' '<<w.coordinate;
    std::cout<<' '<<r.first<<' '<<r.second;
    for (auto word:r.table) std::cout<<' '<<word;
    std::cout<<'\n';
}
}
int main(int argc,char** argv) try {
    if (argc>1 && std::string(argv[1])=="--vectors") {
        for (unsigned seed=0;seed<2048;++seed) output(seed,setup_original_resource_cell(fixture(seed)));
        return 0;
    }
    auto in=fixture(1);
    in.cell=overlay(10); in.randomize=false;
    for (auto& n:in.neighbors) n=overlay(-1);
    for (auto& n:in.neighbor_fallback) n=false;
    (*in.registry)[0].index=0; (*in.registry)[0].value=25; (*in.registry)[0].divisor=12;
    for (unsigned n=0;n<=8;++n) {
        for (unsigned i=0;i<8;++i) in.neighbors[i]=overlay(i<n?10:-1);
        const auto out=setup_original_resource_cell(in);
        constexpr unsigned density[]={0,1,3,4,6,7,8,10,11};
        require(out.status==ResourceCellStatus::Complete && *out.data==density[n] &&
                *out.value==static_cast<int>((density[n]+1)*25),"density formula differs");
    }
    in.cell=overlay(-1); in.registry.reset(); in.randomize.reset();
    require(setup_original_resource_cell(in).value==0,"empty cell needs no registry");
    in.cell=overlay(10); in.registry.reset();
    require(setup_original_resource_cell(in).status==ResourceCellStatus::NeedsInput,"missing registry accepted");
    in=fixture(1); in.cell=overlay(10); in.randomize=false; in.neighbor_fallback[0].reset();
    require(setup_original_resource_cell(in).status==ResourceCellStatus::NeedsInput,"missing neighbor presence accepted");
    in=fixture(1); in.cell=overlay(10); in.randomize=false;
    in.neighbor_fallback[0]=true; in.cell_coordinate=0x8000ffffu; in.neighbors[0].index.reset();
    auto out=setup_original_resource_cell(in);
    require(out.status==ResourceCellStatus::NeedsInput && out.fallback_writes.size()==1 &&
            out.fallback_writes[0].coordinate==0x7fffffffu,"partial fallback write lost");
    in=fixture(1); in.cell=overlay(10); in.randomize=false;
    for (auto& n:in.neighbors) n=overlay(-1);
    for (auto& n:in.neighbor_fallback) n=false;
    (*in.registry)[0].divisor=0;
    require(setup_original_resource_cell(in).status==ResourceCellStatus::Unsupported,"zero divisor accepted");
    (*in.registry)[0].divisor=12; (*in.registry)[0].value.reset();
    out=setup_original_resource_cell(in);
    require(out.status==ResourceCellStatus::NeedsInput && out.data_written,"partial data write lost");
    std::cout<<"Resource density and unknown-state checks pass\n";
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
