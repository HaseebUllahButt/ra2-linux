#include "path_smoothing.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t hash; unsigned count,queries; };
const Golden goldens[]={
#include "full_smoothing_golden.inc"
};
constexpr int offsets[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
std::vector<unsigned> inputs(unsigned seed) {
    std::uint32_t state=seed^0xb8a72e91u;
    std::vector<unsigned> path;
    for (unsigned i=0;i<2+seed%27;++i) {
        state=state*1664525u+1013904223u;
        path.push_back((state>>16)%8);
    }
    if (seed==0) path={0,1,2};
    if (seed==1) path={2,2,0,0};
    if (seed==2) path.assign(25,1);
    const std::vector<std::vector<unsigned>> special={{1,7},{1,1,7,7},{7,1,3,5},{0,4,2,6},{0,0,2,4,4,6},{3,3,1,1,7,7,5,5}};
    if (seed<special.size()) path=special[seed];
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
        const auto result=smooth_original_path({54,69},path,layers,0,[&](const SmoothingQuery& query)->std::optional<SmoothingCell> {
            queries.push_back(query); return reply(query.destination.x,query.destination.y,golden.seed);
        },[&](ra2map::Cell cell)->std::optional<SmoothingCell> { return reply(cell.x,cell.y,golden.seed); });
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
    const SmoothingLookup lookup=[](ra2map::Cell)->std::optional<SmoothingCell> { return SmoothingCell{std::nullopt,0,false,false}; };
    const std::vector<unsigned> path={1,7}; const std::vector<std::optional<int>> layers={0,0};
    require(smooth_original_path({54,69},path,layers,std::nullopt,known,lookup).status==SmoothingStatus::NeedsInput,"unknown threat weight accepted"); ++checks;
    require(smooth_original_path({54,69},path,layers,1,known,lookup).status==SmoothingStatus::Unsupported,"nonzero threat weight accepted"); ++checks;
    require(smooth_original_path({54,69},{8},{0},0,known,lookup).status==SmoothingStatus::Unsupported,"tube smoothing accepted"); ++checks;
    require(smooth_original_path({54,69},path,{0},0,known,lookup).status==SmoothingStatus::Unsupported,"missing layer storage accepted"); ++checks;
    const SmoothingLookup unknown=[](ra2map::Cell)->std::optional<SmoothingCell> { return {}; };
    require(smooth_original_path({54,69},path,layers,0,known,unknown).status==SmoothingStatus::NeedsInput,"missing lookahead cell accepted"); ++checks;
    const SmoothingProbe level_unused=[](const SmoothingQuery&)->std::optional<SmoothingCell> { return SmoothingCell{0,std::nullopt,std::nullopt,false}; };
    const auto result=smooth_original_path({54,69},path,layers,0,level_unused,lookup);
    require(result.status==SmoothingStatus::Complete && result.directions==std::vector<unsigned>({0,0}),"corner helper reads unused entry-cell level"); ++checks;
    const SmoothingLookup unknown_bridge=[](ra2map::Cell)->std::optional<SmoothingCell> { return SmoothingCell{std::nullopt,0,std::nullopt,false}; };
    require(smooth_original_path({54,69},path,{4,4},0,known,unknown_bridge).status==SmoothingStatus::NeedsInput,"unknown lookahead bridge flag accepted"); ++checks;
    require(smooth_original_path({510,2},path,layers,0,known,lookup).status==SmoothingStatus::Unsupported,"lookahead outside the verified grid accepted"); ++checks;
    unsigned lookups=0,entries=0;
    const SmoothingProbe blocked=[&](const SmoothingQuery&)->std::optional<SmoothingCell> { ++entries; return SmoothingCell{7,std::nullopt,std::nullopt,std::nullopt}; };
    const SmoothingLookup counted=[&](ra2map::Cell cell)->std::optional<SmoothingCell> { ++lookups; return lookup(cell); };
    const auto unchanged=smooth_original_path({54,69},path,layers,0,blocked,counted);
    require(unchanged.status==SmoothingStatus::Complete && unchanged.directions==path && lookups==1 && entries==1,"blocked corner omits original next-cell lookup"); ++checks;
    require(smooth_original_path({54,69},path,layers,0,blocked,unknown).status==SmoothingStatus::NeedsInput,"blocked corner silently ignores unknown lookahead state"); ++checks;
    std::cout<<"original full path smoothing and ordered entry probes: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
