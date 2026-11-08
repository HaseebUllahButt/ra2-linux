#include "path.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { int x,y,scenario,count; std::uint32_t cost; int path[25]; };
const Golden goldens[]={
#include "postprocessed_path_golden.inc"
};
using Point=std::pair<int,int>;
const std::array<std::vector<Point>,7> walls={{
    {},{{54,68}},{{54,68},{55,68}},{{53,68},{54,68},{55,68}},
    {{54,68},{53,68},{54,67}},{{54,68},{55,68},{55,69}},
    {{53,68},{54,68},{55,68},{53,67},{55,67}}
}};
bool blocked(int scenario,int x,int y) { return std::find(walls[scenario].begin(),walls[scenario].end(),Point{x,y})!=walls[scenario].end(); }
std::vector<ResolvedGroundPathCell> grid(int scenario) {
    std::vector<ResolvedGroundPathCell> cells;
    for (int y=65;y<74;++y) for (int x=50;x<59;++x) {
        ResolvedGroundPathCell value{{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)},blocked(scenario,x,y)?7:0,EntryHeightCell{}};
        value.marked_40000=false; cells.push_back(value);
    }
    return cells;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_postprocessed_path BASE_EXECUTABLE EXPANSION_EXECUTABLE");
    unsigned checks=0; const PathPostprocessing postprocessing{0};
    for (int edition=1;edition<3;++edition) {
        const auto distance=OriginalDistance::from_executable(argv[edition]);
        for (const auto& golden:goldens) {
            const auto result=find_original_ground_path({54,69},{static_cast<std::uint16_t>(golden.x),static_cast<std::uint16_t>(golden.y)},grid(golden.scenario),distance,-1,nullptr,&postprocessing);
            if (!golden.count) require(result.status==PathStatus::NoPath,"original postprocessed no-path result differs");
            else {
                if (result.status!=PathStatus::Found && result.status!=PathStatus::Partial) {
                    std::cerr<<"scenario="<<golden.scenario<<" goal="<<golden.x<<","<<golden.y<<" "<<result.evidence<<"\n";
                    throw std::runtime_error("original supported postprocessed path rejected");
                }
                std::uint32_t bits; std::memcpy(&bits,&result.cost,4);
                if (bits!=golden.cost || result.directions.size()!=static_cast<unsigned>(golden.count-1)) {
                    std::cerr<<"scenario="<<golden.scenario<<" goal="<<golden.x<<","<<golden.y<<" cost="<<std::hex<<bits<<" expected="<<golden.cost<<std::dec<<" length="<<result.directions.size()<<" expected="<<golden.count-1<<"\n";
                    throw std::runtime_error("original postprocessed cost or length differs");
                }
                for (std::size_t i=0;i<result.directions.size();++i) require(result.directions[i]==static_cast<unsigned>(golden.path[i]),"original postprocessed direction differs");
                require((result.status==PathStatus::Partial)==blocked(golden.scenario,golden.x,golden.y),"original blocked-goal status differs");
            }
            ++checks;
        }
    }
    std::cout<<"original complete regular paths and postprocessing: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
