#pragma once
#include "component_lookup.h"
#include <array>
#include <map>
namespace ra2::sim::test {
inline std::uint32_t component_key(ra2map::Cell c) { return c.x|(std::uint32_t(c.y)<<16); }
inline int component_signed(std::uint16_t v) { return v<0x8000?v:int(v)-0x10000; }
inline ra2map::Cell component_add(ra2map::Cell c,int x,int y) {
    return {static_cast<std::uint16_t>(int(c.x)+x),static_cast<std::uint16_t>(int(c.y)+y)};
}
struct TransitionFixture {
    ra2map::Cell point;
    int tolerance,start;
    std::vector<ComponentTransition> records;
    explicit TransitionFixture(unsigned seed) {
        std::uint32_t state=seed^0x18d6a79bu;
        const auto next=[&](){state=state*1664525u+1013904223u;return state>>16;};
        point={static_cast<std::uint16_t>(next()),static_cast<std::uint16_t>(next())};
        tolerance=int(next()%5)-1;start=next()%5;
        for (unsigned i=0;i<6;++i) {
            const int first_x=int(next()%9)-4;
            const int first_y=int(next()%9)-4;
            auto first=component_add(point,first_x,first_y);
            const int second_x=int(next()%7)-3;
            const int second_y=int(next()%7)-3;
            auto second=component_add(first,second_x,second_y);
            if (seed%4==0 || i==3) {
                if (seed&4) {first=component_add(point,-1,0);second=component_add(point,2,int(seed%7)-3);}
                else {first=component_add(point,0,-1);second=component_add(point,0,2);}
            }
            records.push_back({first,second,static_cast<std::uint8_t>(next()),static_cast<std::int32_t>(next()%4==0?1:0)});
        }
        if (seed%17==0) start=9;
    }
};
struct ComponentFixture {
    unsigned seed,profile,stride;
    CoarseRegions graph;
    MovementRegions movement;
    MovementComponentQuery query;
    ComponentLookupInputs input;
    std::map<int,ComponentCellState> cells;
    std::vector<std::array<std::uint32_t,4>> reads;
    ComponentFixture(unsigned n,DriveEdition edition):seed(n),profile(n%16),stride(9+2*(n%3)) {
        movement.edition=edition;graph.categories.resize(32);graph.categories[0]=7;
        for (unsigned i=0;i<stride*stride;++i) graph.labels.push_back((i*7+n)%32);
        movement.components.resize(original_movement_zone_count(edition));
        for (unsigned zone=0;zone<movement.components.size();++zone) {
            movement.components[zone].resize(32);
            for (unsigned i=0;i<32;++i) movement.components[zone][i]=i?static_cast<std::uint16_t>(n*13+i*37+zone*101):0xffff;
        }
        query.cell={static_cast<std::uint16_t>(3+n%3),static_cast<std::uint16_t>(3+n/3%3)};
        query.movement_zone=(n/16)%movement.components.size();query.above_bridge=profile!=0;
        if (profile==12) {
            constexpr ra2map::Cell edge[]={{0xffff,1},{512,0},{32767,0},{0x8000,64}};
            query.cell=edge[n/16%4];
        }
        const bool vertical=(n&16)==0;
        auto first=component_add(query.cell,vertical?0:-1,vertical?-1:0);
        auto second=component_add(query.cell,vertical?0:2,vertical?2:5);
        ComponentTransition record{first,second,static_cast<std::uint8_t>(profile==3?255:0),0};
        input.transitions=std::vector<ComponentTransition>{record};
        if (profile==2) input.transitions->clear();
        if (profile==11) { input.transitions->at(0).first=second;input.transitions->at(0).second=first; }
        if (profile==13) { auto skipped=record;skipped.field_c=-1;input.transitions->insert(input.transitions->begin(),skipped);input.transitions->push_back(record); }
        if (profile==15) input.transitions->at(0).field_c=7;
        int tile=int(n%16),land=0;input.first_tile_range=100;input.second_tile_range=200;
        if (profile==4 || profile==7 || profile==12 || profile==13) input.first_tile_range=tile;
        if (profile==5) input.second_tile_range=tile;
        if (profile==7) land=3;
        if (profile==8) { input.first_tile_range=-1;tile=0; }
        if (profile==9) { input.first_tile_range=0;tile=(n&16)?16:15; }
        if (profile==10) { input.second_tile_range=-1;tile=14; }
        if (profile==14) { input.first_tile_range=0x7ffffff8;tile=0x7ffffffc;input.second_tile_range=-1; }
        auto raw=query.cell;
        const unsigned length=1+(n/32)%8;
        for (unsigned step=0;step<=length;++step) {
            const auto index=component_signed(raw.y)*512+component_signed(raw.x);
            if (index<0 || index>=262144) break;
            const ra2map::Cell actual{static_cast<std::uint16_t>(index%512),static_cast<std::uint16_t>(index/512)};
            cells[index]={actual,profile!=1 && step<length,tile,land};
            raw=component_add(actual,vertical?0:1,vertical?1:0);
        }
    }
    ComponentCellState metadata(ra2map::Cell point) const {
        const auto index=component_signed(point.y)*512+component_signed(point.x);
        const auto found=cells.find(index);
        return found==cells.end()?ComponentCellState{point,false,0,0}:found->second;
    }
    ComponentCellLookup lookup() {
        return [&](ra2map::Cell p) { auto c=metadata(p);reads.push_back({component_key(p),component_key(*c.object_cell),*c.bridge,0});return c; };
    }
    std::uint32_t digest(const MovementComponentResult& result) const {
        std::uint32_t h=2166136261u;
        const auto add=[&](std::uint32_t v){h=(h^v)*16777619u;};
        add(result.component.value());add(result.transition_index?static_cast<std::uint32_t>(*result.transition_index):0xfffffffeu);
        add(result.selected_cell?component_key(*result.selected_cell):0xffffffffu);add(result.walk_steps);add(result.steps.size());
        for (auto step:result.steps) add(static_cast<unsigned>(step));
        add(reads.size());for (const auto& row:reads) for (auto word:row) add(word);
        return h;
    }
};
}
