#pragma once
#include "nearest_cell.h"
#include <cstdlib>
namespace ra2::sim::test {
inline std::uint32_t nearest_key(ra2map::Cell c) { return c.x|(std::uint32_t(c.y)<<16); }
inline int nearest_signed(std::uint16_t v) { return v<0x8000?v:int(v)-0x10000; }
struct NearestFixture {
    NearestCellInputs input;
    unsigned seed=0,profile=0;
    std::vector<std::array<std::uint32_t,4>> events;
    bool first_read=true;
    explicit NearestFixture(unsigned number,DriveEdition edition,bool edges=false):seed(number),profile(seed%8) {
        auto& q=input.query; q.edition=edition;
        q.goal={static_cast<std::uint16_t>(50+seed%9),static_cast<std::uint16_t>(65+(seed/9)%9)};
        if (edges) {
            constexpr std::uint16_t edge[]={0,1,510,511};q.goal={edge[seed%4],edge[(seed/4)%4]};
        }
        input.empty_cell=NearestEmptyCell{{0,0}};
        q.source=seed%5==0 || profile==7?input.empty_cell->value:ra2map::Cell{static_cast<std::uint16_t>(int(q.goal.x)+int(seed%7)-3),static_cast<std::uint16_t>(int(q.goal.y)+int(seed%11)-5)};
        if (profile==6) q.source=q.goal;
        q.speed_type=(seed/3)%8; q.movement_zone=seed%12;
        q.source_component=seed%11==0?0xffffu:seed%13==0?0xffffffffu:seed+100;
        q.above_bridge=seed%2==0;
        input.map_width=1+seed%16; input.map_height=1+(seed/16)%16;
        if (profile==4) { input.map_width=20; input.map_height=20; }
        if (seed%23==0) { input.map_width=0; input.map_height=0; }
        input.frame=1000+seed*77;
    }
    std::uint32_t hash(ra2map::Cell c) const { return std::uint32_t(nearest_signed(c.x))*17u+std::uint32_t(nearest_signed(c.y))*31u+seed*13u; }
    NearestCellState metadata(ra2map::Cell c) const {
        const auto h=hash(c); const int x=nearest_signed(c.x),y=nearest_signed(c.y);
        const bool special=profile==3 || profile==6;
        const int base=int(seed%7)-3;
        const int level=special?0:nearest_key(c)==nearest_key(input.query.goal)?base:base+int((h>>3)%5)-2;
        return {x>=0 && x<512 && y>=0 && y<512 && (special || profile==4 || h%11!=0),level,special?false:((h>>8)&1)!=0};
    }
    bool fit(const NearestFitQuery& q) const {
        if (profile==4) return false;
        if (profile==3) return true;
        if (profile==6) return q.cell.y==input.query.goal.y && std::abs(nearest_signed(q.cell.x)-input.query.goal.x)==1;
        const auto h=hash(q.cell);
        if ((profile==0 || profile==2) && nearest_key(q.cell)==nearest_key(input.query.goal)) return false;
        return (h>>8)%7==0;
    }
    ra2map::Cell projection(Coord point) const {
        ra2map::Cell cell{static_cast<std::uint16_t>((point.x-128)/256),static_cast<std::uint16_t>((point.y-128)/256)};
        if (profile==3 || profile==5 || (profile!=6 && hash(cell)%4==0)) ++cell.x;
        return cell;
    }
    NearestCellCallbacks callbacks() {
        NearestCellCallbacks cb;
        cb.cell=[&](ra2map::Cell point) {
            const auto value=metadata(point);
            if (first_read) first_read=false;
            else events.push_back({0,nearest_key(point),*value.usable,0});
            return value;
        };
        cb.fits=[&](const NearestFitQuery& q) {
            const auto answer=fit(q);
            events.push_back({1,nearest_key(q.cell),answer,q.component});
            events.push_back({2,static_cast<std::uint32_t>(q.speed_type),q.movement_zone,q.above_bridge});
            return answer;
        };
        cb.project=[&](Coord point) {
            const auto answer=projection(point);
            events.push_back({3,static_cast<std::uint32_t>(point.x),static_cast<std::uint32_t>(point.y),nearest_key(answer)});
            return answer;
        };
        return cb;
    }
    std::uint32_t digest(const NearestCellResult& result) const {
        std::uint32_t out=2166136261u;
        const auto mix=[&](std::uint32_t value) { out=(out^value)*16777619u; };
        mix(nearest_key(result.answer.value()));
        for (const auto& row:events) for (auto word:row) mix(word);
        return out;
    }
};
}
