#include "path_smoothing.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace ra2::sim {
namespace {
constexpr int offsets[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
struct Point { int x,y; };
Point advance(Point p,unsigned direction) { return {p.x+offsets[direction][0],p.y+offsets[direction][1]}; }
struct Window { std::size_t first; Point origin; };
// Original backward window finder. Deleted steps keep their original array slots.
Window window(const std::vector<int>& path,std::size_t end,Point position) {
    int x=0,y=0,maximum=0; bool plateau=false;
    for (auto at=static_cast<std::ptrdiff_t>(end);at>=0;--at) if (path[at]!=-2) {
        const auto reverse=static_cast<unsigned>((path[at]+4)&7);
        x+=offsets[reverse][0]; y+=offsets[reverse][1]; position=advance(position,reverse);
        const int distance=std::max(std::abs(x),std::abs(y));
        if (maximum<distance) {
            maximum=distance;
            if (plateau) return {static_cast<std::size_t>(at+1),advance(position,static_cast<unsigned>(path[at]))};
        } else plateau=true;
    }
    return {0,position};
}
enum class Attempt { Accepted,Blocked,NeedsInput,Unsupported };
Attempt replace(std::vector<int>& path,std::size_t begin,std::size_t count,Point origin,Point delta,
    int layer,const SmoothingProbe& probe) {
    unsigned diagonal=delta.x<0?(delta.y<0?7:5):(delta.y<0?1:3);
    unsigned cardinal=delta.x<=delta.y?(delta.y+delta.x<1?6:4):(delta.y+delta.x<1?0:2);
    int first=std::min(std::abs(delta.x),std::abs(delta.y));
    int second=std::max(std::abs(delta.x),std::abs(delta.y))-first;
    for (unsigned attempt=0;attempt<2;++attempt) {
        if (first) {
            Point position=origin; int current_layer=layer; bool blocked=false;
            for (const auto& segment:std::array<std::pair<unsigned,int>,2>{{{diagonal,first},{cardinal,second}}}) {
                for (int step=0;step<segment.second && !blocked;++step) {
                    position=advance(position,segment.first);
                    if (position.x<0 || position.y<0 || position.x>511 || position.y>511) return Attempt::Unsupported;
                    const auto value=probe({{static_cast<std::uint16_t>(position.x),static_cast<std::uint16_t>(position.y)},segment.first,current_layer});
                    if (!value || !value->entry_status || !value->level) return Attempt::NeedsInput;
                    blocked=*value->entry_status!=0;
                    if (!blocked) {
                        if (!value->marked_40000) return Attempt::NeedsInput;
                        blocked=*value->marked_40000;
                    }
                    const int level=*value->level;
                    if (static_cast<std::int64_t>(current_layer)-level==4) {
                        if (!value->bridge_100) return Attempt::NeedsInput;
                        current_layer=*value->bridge_100?level+4:level;
                    } else current_layer=level;
                }
            }
            if (!blocked) {
                if (static_cast<std::size_t>(first+second)>count) return Attempt::Unsupported;
                auto at=begin;
                for (int i=0;i<first;++i) path[at++]=static_cast<int>(diagonal);
                for (int i=0;i<second;++i) path[at++]=static_cast<int>(cardinal);
                while (at<begin+count) path[at++]=-2;
                return Attempt::Accepted;
            }
        }
        std::swap(diagonal,cardinal); std::swap(first,second);
    }
    return Attempt::Blocked;
}
}
SmoothingResult smooth_original_monotone_path(ra2map::Cell source,const std::vector<unsigned>& directions,
    const std::vector<std::optional<int>>& layers,std::optional<double> threat_weight,const SmoothingProbe& probe) {
    SmoothingResult result; result.directions=directions;
    if (!threat_weight) { result.status=SmoothingStatus::NeedsInput; result.detail="004DC760: actor threat weight is unresolved"; return result; }
    if (!std::isfinite(*threat_weight) || *threat_weight!=0) { result.detail="0042BE20: nonzero actor threat weight is not recovered"; return result; }
    if (source.x>511 || source.y>511 || layers.size()!=directions.size() || !probe) { result.detail="0042B7F0: invalid smoothing grid, layers, or callback"; return result; }
    bool positive_x=false,negative_x=false,positive_y=false,negative_y=false;
    Point position{source.x,source.y};
    for (const auto direction:directions) {
        if (direction>7) { result.detail="0042B7F0: tube directions are not recovered"; return result; }
        positive_x|=offsets[direction][0]>0; negative_x|=offsets[direction][0]<0;
        positive_y|=offsets[direction][1]>0; negative_y|=offsets[direction][1]<0;
        position=advance(position,direction);
        if (position.x<0 || position.y<0 || position.x>511 || position.y>511) { result.detail="0042B7F0: path leaves the verified grid"; return result; }
    }
    if ((positive_x && negative_x) || (positive_y && negative_y)) { result.detail="0042B210/0042B7F0: nonmonotone smoothing is not recovered"; return result; }
    std::vector<int> path(directions.begin(),directions.end());
    position={source.x,source.y}; int x=0,y=0,maximum=0;
    // The original smoothing pass examines only the first twenty raw slots.
    for (std::size_t i=0;i<path.size() && i<20;++i) {
        if (path[i]==-2) continue;
        const auto direction=static_cast<unsigned>(path[i]);
        x+=offsets[direction][0]; y+=offsets[direction][1]; position=advance(position,direction);
        const int distance=std::max(std::abs(x),std::abs(y));
        if (distance<=maximum) {
            const auto span=window(path,i,position);
            if (!layers[span.first]) { result.status=SmoothingStatus::NeedsInput; result.detail="0042B7F0: shortcut layer input is unresolved"; return result; }
            const auto outcome=replace(path,span.first,i-span.first+1,span.origin,{position.x-span.origin.x,position.y-span.origin.y},*layers[span.first],probe);
            if (outcome==Attempt::NeedsInput) { result.status=SmoothingStatus::NeedsInput; result.detail="0042BE20: shortcut cell state is unresolved"; return result; }
            if (outcome==Attempt::Unsupported) { result.detail="0042BE20: shortcut leaves the verified native domain"; return result; }
        }
        maximum=distance;
    }
    result.directions.clear();
    for (const auto direction:path) if (direction!=-2) result.directions.push_back(static_cast<unsigned>(direction));
    result.status=SmoothingStatus::Complete;
    result.detail="0042B7F0/0042BCA0/0042BE20: original monotone smoothing with zero threat contribution";
    return result;
}
}
