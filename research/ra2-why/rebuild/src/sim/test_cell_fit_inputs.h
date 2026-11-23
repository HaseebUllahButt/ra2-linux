#pragma once
#include "cell_fit.h"
#include <array>
#include <cstring>
#include <vector>
namespace ra2::sim::test {
inline std::uint32_t fit_key(ra2map::Cell c) { return c.x|(std::uint32_t(c.y)<<16); }
inline float fit_float(std::uint32_t bits) { float value;std::memcpy(&value,&bits,4);return value; }
struct CellFitFixture {
    ResolvedCellFitQuery query;
    CellFitState cell;
    std::uint32_t actual_component=0;
    std::array<std::uint32_t,108> table{};
    std::vector<std::array<std::uint32_t,4>> events;
    CellFitFixture(unsigned seed,DriveEdition edition) {
        query.edition=edition;query.speed_type=1;
        cell.object_cell=ra2map::Cell{static_cast<std::uint16_t>(seed*31),static_cast<std::uint16_t>(seed*47)};
        cell.level=0;cell.bridge=false;cell.ground_occupancy=0;cell.bridge_occupancy=0;
        cell.overlay_index=-1;cell.land_type=0;cell.overlay=CellFitOverlay{true,false,false};
        table.fill(0x3f800000);
        if (seed<32) {
            query.speed_type=1+(seed&1);*cell.land_type=seed<16?0:5;
            table[*cell.land_type*9+1]=seed<16?((seed/2)&1?0:0x3f800000):0x3f333333;
            table[*cell.land_type*9+2]=seed<16?((seed/2)&1?0x3f800000:0):0x3f000000;
        } else if (seed<2080) {
            const unsigned n=(seed-32)%1024;
            query.ignore_low_five=(n&1)!=0;query.ignore_a0=(n&2)!=0;
            if (seed<1056) cell.ground_occupancy=n/4;
            else { cell.bridge=true;cell.ground_occupancy=255;cell.bridge_occupancy=n/4;table[1]=(seed&4)?0x7fc00001:0; }
        } else if (seed<2208) {
            const auto n=seed-2080;constexpr int heights[]={-128,-1,0,127};
            const int h=heights[n%4];cell.level=h;cell.bridge=(n&4)!=0;query.above_bridge=(n&8)!=0;
            const int levels[]={-1,h,h+4,h+7};query.level=levels[(n/16)%4];
        } else if (seed<2688) {
            const unsigned n=seed-2208;query.movement_zone=int(n%15)-1;
            cell.overlay_index=0;cell.overlay=CellFitOverlay{(n/15&1)!=0,(n/30&1)!=0,(n/60&1)!=0};
            cell.land_type=3;table[3*9+1]=0;
        } else {
            std::uint32_t value=seed^0x612e374au;
            const auto next=[&]() { value=value*1664525u+1013904223u;return value>>16; };
            query.speed_type=next()%8;query.ignore_low_five=next()%2;query.ignore_a0=next()%2;
            query.component=seed%3?0xffffffffu:seed%5?0xffffu:seed;
            actual_component=query.component+(seed%7==0);
            query.movement_zone=int(next()%15)-1;query.above_bridge=next()%2;
            cell.level=int(next()%256)-128;cell.bridge=next()%2;
            const int levels[]={-1,*cell.level,*cell.level+4,*cell.level+1};query.level=levels[next()%4];
            cell.ground_occupancy=seed%5==0?next()%256:0;cell.bridge_occupancy=seed%5==1?next()%256:0;
            cell.overlay_index=seed%3==0?0:-1;cell.overlay=CellFitOverlay{next()%2!=0,next()%2!=0,next()%2!=0};
            cell.land_type=next()%12;
            constexpr std::uint32_t bits[]={0,0x80000000,0x3f800000,0x3f000000,0xbf800000,0x7fc00001,0x7f800001,0x7f800000,0xff800000,1,0x80000001};
            table[*cell.land_type*9+query.speed_type]=bits[next()%11];
        }
    }
    CellFitCallbacks callbacks() {
        CellFitCallbacks cb;
        cb.component=[&](ra2map::Cell c,int zone,bool above) { events.push_back({0,fit_key(c),static_cast<std::uint32_t>(zone),above});return actual_component; };
        cb.land_speed=[&](int land,int speed) { const auto bits=table.at(land*9+speed);events.push_back({1,static_cast<std::uint32_t>(land),static_cast<std::uint32_t>(speed),bits});return fit_float(bits); };
        return cb;
    }
    std::uint32_t digest(const CellFitResult& result) const {
        std::uint32_t h=2166136261u;
        const auto mix=[&](std::uint32_t v){h=(h^v)*16777619u;};mix(result.allowed.value());
        for (const auto& event:events) for (auto v:event) mix(v);
        return h;
    }
};
}
