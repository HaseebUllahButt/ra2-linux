#include "path_smoothing.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t hash; unsigned count,queries; };
const Golden goldens[]={
#include "smoothing_golden.inc"
};
constexpr int offsets[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
std::vector<unsigned> inputs(unsigned seed) {
    std::uint32_t state=seed^0xb8a72e91u;
    std::vector<unsigned> path;
    for (unsigned i=0;i<2+seed%27;++i) {
        state=state*1664525u+1013904223u;
        path.push_back(((seed%4)*2+((state>>16)%3))%8);
    }
    if (seed==0) path={0,1,2};
    if (seed==1) path={2,2,0,0};
    if (seed==2) path.assign(25,1);
    return path;
}
SmoothingCell reply(unsigned x,unsigned y,unsigned seed) {
    return {seed%4!=0 && (x+2*y+seed)%11==0?7:0,static_cast<std::int8_t>(seed%3?static_cast<int>((x+y+seed)%5)-2:0),(x+seed)%2!=0,seed%4!=0 && (2*x+y+seed)%13==0};
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() try {
    unsigned checks=0;
    for (const auto& golden:goldens) {
        const auto path=inputs(golden.seed); int x=54,y=69;
        std::vector<std::optional<int>> layers;
        for (const auto direction:path) {
            layers.push_back(*reply(x,y,golden.seed).level+(golden.seed%5==0?4:0));
            x+=offsets[direction][0]; y+=offsets[direction][1];
        }
        std::vector<SmoothingQuery> queries;
        const auto result=smooth_original_monotone_path({54,69},path,layers,0,[&](const SmoothingQuery& query)->std::optional<SmoothingCell> {
            queries.push_back(query); return reply(query.destination.x,query.destination.y,golden.seed);
        });
        require(result.status==SmoothingStatus::Complete,"original monotone smoothing was rejected");
        std::uint32_t h=2166136261u;
        const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
        add(result.directions.size()); for (const auto direction:result.directions) add(direction);
        add(queries.size()); for (const auto& query:queries) { add(query.destination.x); add(query.destination.y); add(query.direction); add(static_cast<std::uint32_t>(query.layer_height)); }
        if (h!=golden.hash || result.directions.size()!=golden.count || queries.size()!=golden.queries) {
            std::cerr<<"seed="<<golden.seed<<" hash="<<std::hex<<h<<" expected="<<golden.hash<<std::dec<<" length="<<result.directions.size()<<" expected="<<golden.count<<" queries="<<queries.size()<<" expected="<<golden.queries<<"\n";
            throw std::runtime_error("original smoothing directions or ordered probes differ");
        }
        ++checks;
    }
    const SmoothingProbe known=[](const SmoothingQuery&)->std::optional<SmoothingCell> { return SmoothingCell{0,0,false,false}; };
    const std::vector<unsigned> path={0,1,2}; const std::vector<std::optional<int>> layers={0,0,0};
    require(smooth_original_monotone_path({54,69},path,layers,std::nullopt,known).status==SmoothingStatus::NeedsInput,"unknown threat weight accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},path,layers,1,known).status==SmoothingStatus::Unsupported,"nonzero threat weight accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},{0,4},{0,0},0,known).status==SmoothingStatus::Unsupported,"nonmonotone smoothing accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},{8},{0},0,known).status==SmoothingStatus::Unsupported,"tube smoothing accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},path,{0},0,known).status==SmoothingStatus::Unsupported,"missing layer storage accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},path,{std::nullopt,0,0},0,known).status==SmoothingStatus::NeedsInput,"unknown shortcut layer accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},path,layers,0,[](const SmoothingQuery&)->std::optional<SmoothingCell> { return {}; }).status==SmoothingStatus::NeedsInput,"unknown shortcut cell accepted"); ++checks;
    require(smooth_original_monotone_path({54,69},path,layers,0,[](const SmoothingQuery&)->std::optional<SmoothingCell> { return SmoothingCell{0,0,false,std::nullopt}; }).status==SmoothingStatus::NeedsInput,"unknown required marked flag accepted"); ++checks;
    const auto blocked=smooth_original_monotone_path({54,69},path,layers,0,[](const SmoothingQuery&)->std::optional<SmoothingCell> { return SmoothingCell{7,0,std::nullopt,std::nullopt}; });
    require(blocked.status==SmoothingStatus::Complete && blocked.directions==path,"blocked probe reads unused mark or bridge flags"); ++checks;
    require(smooth_original_monotone_path({0,0},{0},{0},0,known).status==SmoothingStatus::Unsupported,"grid underflow accepted"); ++checks;
    std::cout<<"original monotone smoothing and ordered entry probes: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
