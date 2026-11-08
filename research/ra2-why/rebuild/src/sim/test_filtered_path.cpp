#include "path.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { int x,y,profile,count; std::uint32_t cost; int path[8]; };
const Golden goldens[]={
#include "filtered_path_golden.inc"
};
std::vector<ResolvedGroundPathCell> grid(int profile) {
    std::vector<ResolvedGroundPathCell> cells;
    for (int y=65;y<74;++y) for (int x=50;x<59;++x) {
        ResolvedGroundPathCell cell{{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)},0,EntryHeightCell{}};
        cell.hierarchy_zone=x>54?2:1;
        cell.occupancy_neighbors=profile==24?255:(profile==21 && y==67 && x>54?1:0);
        cells.push_back(cell);
    }
    return cells;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_filtered_path BASE_EXECUTABLE EXPANSION_EXECUTABLE");
    unsigned checks=0;
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (const auto& golden:goldens) {
            PathHierarchyFilter filter{{false,true,golden.profile==23}};
            const auto result=find_original_ground_path({54,69},{static_cast<std::uint16_t>(golden.x),static_cast<std::uint16_t>(golden.y)},grid(golden.profile),distance,-1,&filter);
            if (!golden.count) require(result.status==PathStatus::NoPath,"original filtered no-path result differs");
            else {
                std::uint32_t bits; std::memcpy(&bits,&result.cost,4);
                require(bits==golden.cost,"original filtered path cost differs");
                if (golden.profile==21 && golden.x==56 && golden.y==67) {
                    require(result.status==PathStatus::Unsupported && result.directions==std::vector<unsigned>({0,1,2}),"unrecovered monotone shortcut accepted");
                } else {
                    require(result.status==PathStatus::Found,"original filtered path rejected");
                    require(result.directions.size()==static_cast<unsigned>(golden.count-1),"original filtered path length differs");
                    for (std::size_t i=0;i<result.directions.size();++i) require(result.directions[i]==static_cast<unsigned>(golden.path[i]),"original filtered path direction differs");
                }
            }
            ++checks;
        }
        PathHierarchyFilter filter{{false,true,false}};
        auto cells=grid(20); for (auto& cell:cells) cell.hierarchy_zone.reset();
        require(find_original_ground_path({54,69},{54,67},cells,distance,-1,&filter).status==PathStatus::NeedsInput,"unknown hierarchy zone accepted"); ++checks;
        cells=grid(20); for (auto& cell:cells) cell.occupancy_neighbors.reset();
        require(find_original_ground_path({54,69},{54,67},cells,distance,-1,&filter).status==PathStatus::NeedsInput,"unknown outside-route neighbor count accepted"); ++checks;
        filter.allowed_zones.resize(2); cells=grid(20);
        require(find_original_ground_path({54,69},{54,67},cells,distance,-1,&filter).status==PathStatus::NeedsInput,"missing allowed-zone mark accepted"); ++checks;
        filter.allowed_zones={false,true,true}; for (auto& cell:cells) cell.occupancy_neighbors.reset();
        require(find_original_ground_path({54,69},{54,67},cells,distance,-1,&filter).status==PathStatus::Found,"allowed zone reads an unused neighbor count"); ++checks;
    }
    std::cout<<"original hierarchy-filtered regular paths and explicit gaps: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
