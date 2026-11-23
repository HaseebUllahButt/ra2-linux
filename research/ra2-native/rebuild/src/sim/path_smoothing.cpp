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
Window window(const std::vector<int>& path,std::size_t end,Point position,std::size_t begin=0) {
    int x=0,y=0,maximum=0; bool plateau=false;
    for (auto at=static_cast<std::ptrdiff_t>(end);at>=static_cast<std::ptrdiff_t>(begin);--at) if (path[at]!=-2) {
        const auto reverse=static_cast<unsigned>((path[at]+4)&7);
        x+=offsets[reverse][0]; y+=offsets[reverse][1]; position=advance(position,reverse);
        const int distance=std::max(std::abs(x),std::abs(y));
        if (maximum<distance) {
            maximum=distance;
            if (plateau) return {static_cast<std::size_t>(at+1),advance(position,static_cast<unsigned>(path[at]))};
        } else plateau=true;
    }
    return {begin,position};
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
struct Corner { Attempt status; int consumed; Point origin; };
bool in_grid(Point point) { return point.x>=0 && point.y>=0 && point.x<=511 && point.y<=511; }
Corner corner(std::vector<int>& path,const std::vector<std::optional<int>>& layers,std::size_t begin,
    int first,int second,Point origin,const SmoothingProbe& probe,const SmoothingLookup& lookup) {
    const auto first_direction=static_cast<unsigned>(path[begin]);
    const auto second_direction=static_cast<unsigned>(path[begin+first]);
    auto middle=(first_direction+second_direction)/2;
    if (middle+1!=first_direction && middle+1!=second_direction) middle=0;
    second=std::min(first,second);
    Point candidate=origin;
    for (int i=0;i<first-second;++i) candidate=advance(candidate,first_direction);
    for (int size=second;size>0;--size) {
        const auto layer=layers.at(begin+first-size);
        if (!layer) return {Attempt::NeedsInput,0,origin};
        int current_layer=*layer;
        Point position=advance(candidate,middle); bool blocked=false;
        for (int step=0;step<size*2 && !blocked;++step) {
            if (!in_grid(position)) return {Attempt::Unsupported,0,origin};
            const auto value=probe({{static_cast<std::uint16_t>(position.x),static_cast<std::uint16_t>(position.y)},middle,current_layer});
            if (!value || !value->entry_status) return {Attempt::NeedsInput,0,origin};
            blocked=*value->entry_status!=0;
            if (!blocked) {
                if (!value->marked_40000) return {Attempt::NeedsInput,0,origin};
                blocked=*value->marked_40000;
            }
            // 0042B420 reads the NEXT cell even after a blocked or final entry probe.
            position=advance(position,middle);
            if (!in_grid(position)) return {Attempt::Unsupported,0,origin};
            const auto next=lookup({static_cast<std::uint16_t>(position.x),static_cast<std::uint16_t>(position.y)});
            if (!next || !next->level) return {Attempt::NeedsInput,0,origin};
            const int level=*next->level;
            if (static_cast<std::int64_t>(current_layer)-level==4) {
                if (!next->bridge_100) return {Attempt::NeedsInput,0,origin};
                current_layer=*next->bridge_100?level+4:level;
            } else current_layer=level;
        }
        if (!blocked) {
            const int consumed=first-size;
            for (int i=0;i<size*2;++i) path.at(begin+consumed+i)=static_cast<int>(middle);
            return {Attempt::Accepted,consumed,candidate};
        }
        candidate=advance(candidate,first_direction);
    }
    for (int i=0;i<first;++i) origin=advance(origin,first_direction);
    return {Attempt::Blocked,first,origin};
}
Attempt corner_pass(std::vector<int>& path,const std::vector<std::optional<int>>& layers,Point source,
    const SmoothingProbe& probe,const SmoothingLookup& lookup) {
    std::size_t begin=0,run=0,second_start=0,second_count=0,iterations=0;
    int previous=-1,second_direction=-1; bool pending=false;
    Point position=source,origin=source;
    const auto apply=[&]() {
        const auto value=corner(path,layers,begin,static_cast<int>(run),static_cast<int>(second_count),origin,probe,lookup);
        if (value.status==Attempt::NeedsInput || value.status==Attempt::Unsupported) return value.status;
        begin+=value.consumed; run=1; pending=false; origin=value.origin;
        previous=path.at(begin); position=advance(origin,static_cast<unsigned>(previous));
        return Attempt::Accepted;
    };
    while (begin+run<path.size()) {
        if (++iterations>1000000) return Attempt::Unsupported;
        if (path.size()<=second_start+second_count) break;
        if (pending) {
            if (path.at(second_start+second_count)==second_direction) ++second_count;
            else {
                const auto status=apply();
                if (status==Attempt::NeedsInput || status==Attempt::Unsupported) return status;
            }
        } else {
            const auto at=begin+run; const int direction=path.at(at);
            const auto change=static_cast<unsigned>(direction-previous)&7u;
            if (direction==previous) ++run;
            else if ((change==2 || change==6) && previous!=-1) {
                pending=true; second_count=1; second_start=at; second_direction=direction;
            } else {
                run=1; previous=(direction&1)?direction:-1; origin=position; begin=at;
            }
            position=advance(position,static_cast<unsigned>(direction));
        }
    }
    if (pending) {
        const auto value=corner(path,layers,begin,static_cast<int>(run),static_cast<int>(second_count),origin,probe,lookup);
        return value.status;
    }
    return Attempt::Accepted;
}
Attempt general_pass(std::vector<int>& path,const std::vector<std::optional<int>>& layers,Point source,const SmoothingProbe& probe) {
    Point position=source,anchor{0,0},recent{0,0},earlier{0,0};
    int maximum_x=0,maximum_y=0,maximum=0;
    std::size_t index=0,last_reversal=0,earlier_index=0;
    while (index<path.size() && index<20) {
        if (path[index]==-2) { ++index; continue; }
        const auto direction=static_cast<unsigned>(path[index]);
        const auto next=advance(recent,direction),old_next=advance(earlier,direction);
        const int x=std::abs(next.x),y=std::abs(next.y);
        if (x<maximum_x || y<maximum_y) {
            if (!anchor.x && !anchor.y) {
                maximum_x=maximum_y=0; recent={0,0}; last_reversal=index; anchor=position;
            } else {
                earlier_index=last_reversal; earlier={anchor.x-position.x,anchor.y-position.y};
                maximum_x=maximum_y=0; recent={0,0}; maximum=std::max(std::abs(earlier.x),std::abs(earlier.y));
                last_reversal=index; anchor=position;
            }
        } else {
            recent=next; const int distance=std::max(std::abs(old_next.x),std::abs(old_next.y));
            position=advance(position,direction);
            if (distance<=maximum) {
                const auto span=window(path,index,position,earlier_index);
                if (!layers.at(span.first)) return Attempt::NeedsInput;
                const auto status=replace(path,span.first,index-span.first+1,span.origin,{position.x-span.origin.x,position.y-span.origin.y},*layers[span.first],probe);
                if (status==Attempt::NeedsInput || status==Attempt::Unsupported) return status;
            }
            maximum=std::max(maximum,distance); ++index; earlier=old_next; maximum_x=x; maximum_y=y;
        }
    }
    if ((anchor.x || anchor.y) && std::max(std::abs(position.x-anchor.x),std::abs(position.y-anchor.y))<static_cast<int>(index-last_reversal)-1) {
        const auto span=window(path,index-1,position,last_reversal);
        if (!layers.at(span.first)) return Attempt::NeedsInput;
        const auto status=replace(path,span.first,index-span.first,span.origin,{position.x-span.origin.x,position.y-span.origin.y},*layers[span.first],probe);
        if (status==Attempt::NeedsInput || status==Attempt::Unsupported) return status;
    }
    return Attempt::Accepted;
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
SmoothingResult smooth_original_path(ra2map::Cell source,const std::vector<unsigned>& directions,
    const std::vector<std::optional<int>>& layers,std::optional<double> threat_weight,
    const SmoothingProbe& probe,const SmoothingLookup& lookup) {
    SmoothingResult result; result.directions=directions;
    if (!threat_weight) { result.status=SmoothingStatus::NeedsInput; result.detail="004DC760: actor threat weight is unresolved"; return result; }
    if (!std::isfinite(*threat_weight) || *threat_weight!=0) { result.detail="0042B420/0042BE20: nonzero actor threat weight is not recovered"; return result; }
    if (source.x>511 || source.y>511 || layers.size()!=directions.size() || !probe || !lookup) { result.detail="0042B210: invalid smoothing grid, layers, or callbacks"; return result; }
    Point position{source.x,source.y};
    for (const auto direction:directions) {
        if (direction>7) { result.detail="0042B210: tube directions are not recovered"; return result; }
        position=advance(position,direction);
        if (!in_grid(position)) { result.detail="0042B210: raw path leaves the verified grid"; return result; }
    }
    std::vector<int> path(directions.begin(),directions.end());
    auto status=corner_pass(path,layers,{source.x,source.y},probe,lookup);
    if (status!=Attempt::NeedsInput && status!=Attempt::Unsupported) status=general_pass(path,layers,{source.x,source.y},probe);
    if (status==Attempt::NeedsInput) { result.status=SmoothingStatus::NeedsInput; result.detail="0042B210/0042B7F0: required smoothing input is unresolved"; return result; }
    if (status==Attempt::Unsupported) { result.detail="0042B210/0042B7F0: smoothing leaves the verified native domain"; return result; }
    result.directions.clear(); for (const auto direction:path) if (direction!=-2) result.directions.push_back(static_cast<unsigned>(direction));
    result.status=SmoothingStatus::Complete; result.detail="0042B210/0042B7F0: original no-tube path smoothing with resolved zero threat"; return result;
}

}
