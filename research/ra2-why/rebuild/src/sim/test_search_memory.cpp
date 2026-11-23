#include "path.h"
#include "hierarchy_route.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ra2::sim;
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::vector<ResolvedGroundPathCell> grid(int profile) {
    std::vector<ResolvedGroundPathCell> cells;
    for (unsigned y=65;y<74;++y) for (unsigned x=50;x<59;++x) {
        ResolvedGroundPathCell cell{{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)},0,EntryHeightCell{}};
        cell.hierarchy_zone=profile<0?1:profile==26?(x==54 && y==68?2:1):(x>54?2:1);
        cell.occupancy_neighbors=profile==24?255:profile==21 && y==67 && x>54?1:0;
        cell.marked_40000=false; cells.push_back(cell);
    }
    return cells;
}
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_search_memory BASE_EXECUTABLE EXPANSION_EXECUTABLE [--vectors]");
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        unsigned number=0; std::uint32_t total=2166136261u;
        for (unsigned pattern=0;pattern<4;++pattern) for (int profile:{-1,20,21,23,24,26})
            for (ra2map::Cell goal:std::vector<ra2map::Cell>{{56,67},{52,67},{54,67},{58,69},{54,69}}) {
                OriginalSearchMemory memory;
                for (unsigned i=0;i<1500;++i) memory.paths[i/500][i%500]=pattern==0?0:pattern==1?1:pattern==2?(i%2?1:2):2;
                if (pattern==3) memory.paths[0][0]=1;
                memory.counts={pattern?2u:0u,3,4};
                PathHierarchyFilter filter{{false,true,profile==23}}; PathPostprocessing post{0};
                const auto result=find_original_ground_path({54,69},goal,grid(profile),distance,-1,profile<0?nullptr:&filter,&post,&memory);
                require(result.status==PathStatus::Found || result.status==PathStatus::Partial || result.status==PathStatus::NoPath,"retained search state rejected a resolved path");
                std::uint32_t h=2166136261u;
                const auto mix=[&](std::uint32_t value) { h=(h^value)*16777619u; };
                mix(memory.progress_6c); mix(memory.progress_cell_70.x|(static_cast<std::uint32_t>(memory.progress_cell_70.y)<<16));
                for (auto count:memory.counts) mix(count);
                for (const auto& row:memory.paths) for (auto word:row) mix(word);
                const bool found=result.status==PathStatus::Found || result.status==PathStatus::Partial;
                mix(found?result.directions.size()+1:0);
                if (found) { for (auto direction:result.directions) mix(direction); mix(0xffffffffu); }
                std::uint32_t cost=0; if (found) std::memcpy(&cost,&result.cost,4); mix(cost);
                if (argc==4) std::cout<<edition<<" "<<number<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<h<<std::dec<<"\n";
                ++number; total=(total^h)*16777619u;
            }
        if (argc==3) {
            std::cout<<"edition "<<edition<<" retained search digest "<<std::hex<<total<<std::dec<<"\n";
            require(total==0xb1259f48u,"original retained search progress differs");
        }
    }
    OriginalSearchMemory memory;
    bool zero=true; for (const auto& row:memory.paths) for (auto word:row) zero&=word==0;
    require(zero && memory.progress_6c==-1 && memory.progress_cell_70.x==0 && memory.counts==std::array<std::uint32_t,3>{0,0,0},"original search constructor fields differ");
    for (auto& row:memory.paths) row.fill(33);
    memory.counts={8,9,10}; memory.progress_6c=4; memory.progress_cell_70={5,6};
    HierarchyRoute partial; partial.layers[2].path={7,8}; partial.layers[1].path={4};
    retain_original_hierarchy_paths(memory,partial);
    require(memory.paths[2][0]==7 && memory.paths[2][1]==8 && memory.paths[2][2]==33 && memory.paths[1][0]==4 && memory.paths[1][1]==33 && memory.paths[0][0]==33,"hierarchy writes lost retained path tails");
    require(memory.counts==std::array<std::uint32_t,3>{8,1,2} && memory.progress_6c==4 && memory.progress_cell_70.x==5,"hierarchy writes changed untouched search state");
    partial.layers[2].path.resize(501);
    bool invalid=false; try { retain_original_hierarchy_paths(memory,partial); } catch(const std::invalid_argument&) { invalid=true; }
    require(invalid && memory.paths[2][2]==33 && memory.counts[2]==2,"oversized hierarchy prefix changed retained state");
    const auto distance=OriginalDistance::from_executable(argv[1]);
    auto unknown=grid(-1); for (auto& cell:unknown) cell.hierarchy_zone.reset();
    require(find_original_ground_path({54,69},{56,67},unknown,distance,-1,nullptr,nullptr,&memory).status==PathStatus::NeedsInput,"unknown generated-cell region was accepted");
    std::vector<ResolvedGroundPathCell> large;
    for (unsigned y=0;y<40;++y) for (unsigned x=0;x<100;++x) {
        ResolvedGroundPathCell cell{{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)},x==50?7:0,EntryHeightCell{}};
        cell.hierarchy_zone=1; large.push_back(cell);
    }
    for (auto& row:memory.paths) row.fill(1);
    const auto exhausted=find_original_ground_path({10,20},{90,20},large,distance,-1,nullptr,nullptr,&memory);
    require(exhausted.status==PathStatus::Unsupported && exhausted.evidence.find("three retained path buffers")!=std::string::npos && memory.progress_6c==1499,"out-of-buffer progress was accepted");
    if (argc==3) std::cout<<"240 original retained search vectors and six constructor, prefix, or input checks pass\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
