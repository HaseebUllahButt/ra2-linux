#pragma once
#include "local_reachability.h"
#include <algorithm>
namespace ra2::sim::test {
struct LocalReachFixture {
    struct Cell { std::uint16_t region=0; std::int8_t height=0; unsigned category=0; int entry=0; bool usable=false; };
    LocalReachabilityInputs inputs;
    LocalReachabilityCallbacks callbacks;
    std::array<Cell,256> cells;
    std::array<std::vector<std::uint16_t>,7> links;
    std::vector<std::array<std::int32_t,4>> events;
    static std::size_t index(ra2map::Cell cell) {
        const auto signed16=[](std::uint16_t value) { return value<0x8000?int(value):int(value)-0x10000; };
        return std::clamp(signed16(cell.y)*16+signed16(cell.x),0,255);
    }
    explicit LocalReachFixture(unsigned seed) {
        std::uint32_t value=seed^0xd39a6721;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        inputs.level=next()%3; inputs.movement_zone=next()%12;
        if (seed>=4096) { inputs.edition=DriveEdition::YurisRevenge; inputs.movement_zone=12; }
        const unsigned x=next()%8+4,y=next()%8+4;
        inputs.source=y*16+x;
        for (auto& cell:cells) {
            cell.region=next()%7; cell.height=static_cast<std::int8_t>(int(next()%32)-16);
            cell.category=next()%8; cell.entry=next()%3?7:0; cell.usable=next()%4!=0;
            if (seed%8==0) { cell.region=1; cell.entry=0; cell.usable=true; }
            if (seed%8==1) { cell.region=1; cell.category=0; cell.usable=true; }
            if (seed%8==2) cell.entry=0;
        }
        for (auto& node:links) {
            const unsigned count=next()%12;
            for (unsigned i=0;i<count;++i) node.push_back(next()%7);
        }
        for (unsigned i=0;i<seed%4;++i) inputs.initial_regions.push_back((seed>>i)&7);
        callbacks.coordinates=[](ReachabilityCell cell)->std::optional<ra2map::Cell> {
            return ra2map::Cell{static_cast<std::uint16_t>(cell%16),static_cast<std::uint16_t>(cell/16)};
        };
        callbacks.region=[&](ReachabilityCell cell,unsigned)->std::optional<std::uint16_t> { return cells.at(cell).region; };
        callbacks.neighbor=[](ReachabilityCell cell,unsigned direction)->std::optional<ReachabilityCell> {
            static constexpr int dx[]={0,1,1,1,0,-1,-1,-1},dy[]={-1,-1,0,1,1,1,0,-1};
            const int x=std::clamp(int(cell%16)+dx[direction],0,15),y=std::clamp(int(cell/16)+dy[direction],0,15);
            return y*16+x;
        };
        callbacks.height=[&](ReachabilityCell cell)->std::optional<std::int8_t> { return cells.at(cell).height; };
        callbacks.entry=[&](ReachabilityCell cell,unsigned direction,std::int8_t height)->std::optional<int> {
            events.push_back({0,static_cast<int>(cell),static_cast<int>(direction),height});
            return cells.at(cell).entry;
        };
        callbacks.category=[&](ReachabilityCell cell)->std::optional<unsigned> { return cells.at(cell).category; };
        callbacks.usable=[&](ra2map::Cell cell)->std::optional<bool> {
            events.push_back({1,cell.x,cell.y,0}); return cells.at(index(cell)).usable;
        };
        callbacks.scan_region=[&](ra2map::Cell cell,unsigned)->std::optional<std::uint16_t> { return cells.at(index(cell)).region; };
        callbacks.links=[&](std::uint16_t region,unsigned)->std::optional<std::vector<std::uint16_t>> { return links.at(region); };
    }
    std::uint32_t digest(const LocalReachabilityResult& out) const {
        std::uint32_t h=2166136261u;
        const auto mix=[&](std::uint32_t v) { h=(h^v)*16777619u; };
        mix(static_cast<unsigned>(out.status)); mix(out.reachable); mix(out.regions.size());
        for (auto v:out.regions) mix(v);
        const unsigned size=1u<<(inputs.level+1);
        for (unsigned x=0;x<size;++x) for (unsigned y=0;y<size;++y) mix(out.visited[x*8+y]);
        for (const auto& event:events) for (auto v:event) mix(v);
        return h;
    }
};
}
