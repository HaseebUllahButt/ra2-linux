#pragma once
#include "test_cell_fit_inputs.h"
namespace ra2::sim::test {
inline int fit_signed(std::uint16_t x) { return x<0x8000?x:int(x)-0x10000; }
struct RectangleFitFixture {
    unsigned seed,profile;
    RectangleFitQuery query;
    std::array<std::uint32_t,108> table{};
    std::vector<std::array<std::uint32_t,4>> events;
    RectangleFitFixture(unsigned n,DriveEdition edition):seed(n),profile(n/64%8) {
        constexpr std::uint16_t edge[]={0,1,510,511,0xffff,0x7fff,0x8000,0xfffe};
        query.edition=edition;query.origin={edge[n%8],edge[n/8%8]};
        query.width=n%5+1;query.height=n/5%5+1;
        if (n%29==0) query.width=0;
        if (n%31==0) query.height=-1;
        query.speed_type=n%8;query.component=n%3==0?n+400:0xffffffffu;
        query.movement_zone=int(n/7%15)-1;query.above_bridge=n&1;query.reject_overlay=n%4==0;
        if (profile==6) query.level=int(n%9)-4+(n%3==0?4:0);
        table.fill(0x3f800000);
        for (int land=0;land<12;++land) {
            if (profile==1) { table[land*9+1]=0;table[land*9+2]=0x3f000000; }
            if (profile==3) for (int speed=0;speed<8;++speed) table[land*9+speed]=0;
            if (profile==7) for (int speed=0;speed<8;++speed) {
                constexpr std::uint32_t bits[]={0,0x80000000,0x3f800000,0x7fc00001,0xbf800000,0x7f800000};
                table[land*9+speed]=bits[(land+speed+n)%6];
            }
        }
    }
    bool mapped(ra2map::Cell point,int& index) const {
        index=fit_signed(point.y)*512+fit_signed(point.x);
        return index>=0 && index<262144 && unsigned(index)%7!=seed%7;
    }
    CellFitState metadata(ra2map::Cell point) const {
        int index;const bool valid=mapped(point,index);
        const auto h=(valid?std::uint32_t(index):0xffffffffu)^seed*0x9e3779b9u;
        CellFitState c;
        c.object_cell=valid?ra2map::Cell{static_cast<std::uint16_t>(index%512),static_cast<std::uint16_t>(index/512)}:point;
        c.level=int(seed%9)-4;c.bridge=(profile==2 || profile==3 || profile==6) && (h&1);
        c.ground_occupancy=profile==2?((h>>4)&255):profile==3?255:0;
        c.bridge_occupancy=profile==2?((h>>12)&255):0;
        c.overlay_index=profile==4 && (h&4)?0:-1;c.overlay=CellFitOverlay{true,true,(seed&1)!=0};
        c.land_type=(h>>20)%12;return c;
    }
    std::uint32_t component(ra2map::Cell point) const { return query.component+(profile==5 && ((fit_key(point)^seed)&1)); }
    CellFitLookup lookup() {
        return [&](ra2map::Cell p) { auto c=metadata(p);events.push_back({2,fit_key(p),fit_key(*c.object_cell),0});return c; };
    }
    CellFitCallbacks callbacks() {
        CellFitCallbacks cb;
        cb.component=[&](ra2map::Cell p,int zone,bool above) { events.push_back({0,fit_key(p),static_cast<std::uint32_t>(zone),above});return component(p); };
        cb.land_speed=[&](int land,int speed) { const auto bits=table.at(land*9+speed);events.push_back({1,static_cast<std::uint32_t>(land),static_cast<std::uint32_t>(speed),bits});return fit_float(bits); };
        return cb;
    }
    std::uint32_t digest(const RectangleFitResult& result) const {
        std::uint32_t h=2166136261u;const auto mix=[&](std::uint32_t v){h=(h^v)*16777619u;};mix(result.allowed.value());
        for (const auto& event:events) for (auto v:event) mix(v);
        return h;
    }
};
}
