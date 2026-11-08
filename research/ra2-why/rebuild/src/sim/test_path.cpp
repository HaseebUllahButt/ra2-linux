#include "path.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { int x,y,scenario,count; std::uint32_t cost; int path[25]; };
const Golden goldens[]={
#include "path_golden.inc"
};
std::vector<ResolvedFlatPathCell> grid(int scenario) {
    std::vector<ResolvedFlatPathCell> cells;
    for (int y=65;y<74;++y) for (int x=50;x<59;++x) {
        const bool blocked=(scenario==1 && x==54 && y==67) || (scenario==2 && y==68) || (scenario==3 && x==55 && y==68) || (scenario==4 && x==54 && y==68);
        cells.push_back({{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)},blocked?7:0});
    }
    return cells;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_path BASE_EXECUTABLE EXPANSION_EXECUTABLE");
    unsigned count=0;
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (const auto& golden:goldens) {
            const auto result=find_original_flat_path({54,69},{static_cast<std::uint16_t>(golden.x),static_cast<std::uint16_t>(golden.y)},grid(golden.scenario),distance);
            if (!golden.count) require(result.status==PathStatus::NoPath,"original no-path result differs");
            else {
                if (result.status!=PathStatus::Found && result.status!=PathStatus::Partial) {
                    std::cerr<<golden.x<<","<<golden.y<<" scenario="<<golden.scenario<<" "<<result.evidence<<"\n";
                    throw std::runtime_error("original supported path rejected");
                }
                std::uint32_t bits; std::memcpy(&bits,&result.cost,4);
                if (bits!=golden.cost || result.directions.size()!=static_cast<unsigned>(golden.count-1)) {
                    std::cerr<<golden.x<<","<<golden.y<<" scenario="<<golden.scenario<<" cost="<<std::hex<<bits<<" expected="<<golden.cost<<std::dec<<" length="<<result.directions.size()<<" expected="<<golden.count-1<<"\n";
                    throw std::runtime_error("original path cost or length differs");
                }
                for (std::size_t i=0;i<result.directions.size();++i) require(result.directions[i]==static_cast<unsigned>(golden.path[i]),"original direction queue differs");
                require((result.status==PathStatus::Partial)==(golden.scenario==1),"original blocked-goal stop differs");
            }
            ++count;
        }
        auto unknown=grid(0); for (auto& cell:unknown) if (cell.cell.x==54 && cell.cell.y==68) cell.entry_status.reset();
        require(find_original_flat_path({54,69},{54,67},unknown,distance).status==PathStatus::NeedsInput,"unknown eligibility accepted"); ++count;
        for (auto& cell:unknown) if (cell.cell.x==54 && cell.cell.y==68) cell.entry_status=2;
        require(find_original_flat_path({54,69},{54,67},unknown,distance).status==PathStatus::Unsupported,"unrecovered occupant cost accepted"); ++count;
        require(find_original_flat_path({54,69},{54,67},grid(0),distance,1).status==PathStatus::NoPath,"original search limit ignored"); ++count;
        require(find_original_flat_path({54,69},{54,67},grid(4),distance).status==PathStatus::Unsupported,"nonmonotone postprocessing accepted"); ++count;
    }
    std::cout<<"original flat regular paths and explicit limits: "<<count<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
