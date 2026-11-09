#pragma once
#include "cell_projection.h"
#include <array>
#include <vector>
namespace ra2::sim::test {
inline std::uint32_t projection_key(ra2map::Cell c) { return c.x|(std::uint32_t(c.y)<<16); }
inline int projection_signed(std::uint16_t v) { return v<0x8000?v:int(v)-0x10000; }
struct ProjectionFixture {
    unsigned seed=0,profile=0;
    Coord point;
    ra2map::Cell source;
    std::vector<std::array<std::uint32_t,4>> events;
    explicit ProjectionFixture(unsigned number):seed(number),profile(seed%8) {
        std::uint32_t state=seed^0x6d1c7a32u;
        const auto next=[&]() { state=state*1664525u+1013904223u;return state>>16; };
        point.x=(int(next()%516)-2)*256;point.x+=int(next()%511)-255;
        point.y=(int(next()%516)-2)*256;point.y+=int(next()%511)-255;
        std::uint32_t bits=next()<<16;bits|=next();
        point.z=bits<=0x7fffffffu?int(bits):-1-int(0xffffffffu-bits);
        constexpr int edge[]={-2147483647-1,2147483647,-257,-256,-255,-129,-128,-1,0,1,127,128,255,256,257,8388352};
        if (seed%3==0) point.x=edge[seed%16];
        if (seed%5==0) point.y=edge[(seed/5)%16];
        source={static_cast<std::uint16_t>(point.x/256),static_cast<std::uint16_t>(point.y/256)};
    }
    std::uint32_t hash(ra2map::Cell c) const {
        return std::uint32_t(projection_signed(c.x))*0x045d9f3bu+std::uint32_t(projection_signed(c.y))*0x119de1f3u+seed*0x9e3779b9u;
    }
    ProjectionCellState metadata(ra2map::Cell c) const {
        const auto h=hash(c);int level=0;
        if (projection_key(c)==projection_key(source)) level=profile==2?127:profile==3?-128:profile==0 || profile==4 || profile==5?0:int(seed%7)-3;
        else if (profile==1) level=int(seed%7)-3+int((h>>3)%11)-5;
        else if (profile==2) level=-128;
        else if (profile==3) level=127;
        else if (profile==6) level=int(h%31)-15;
        else if (profile==7) level=int(h&255)-128;
        const bool flag=profile==0?false:profile==4 || profile==5?true:seed%2==0;
        const bool bridge=profile==0 || profile==5?false:profile==4?true:(h&0x100)!=0;
        return {level,flag,bridge};
    }
    std::uint32_t flags(const ProjectionCellState& c) const {
        return (seed*2654435761u&~0x1100u)|(*c.flag_1000?0x1000u:0u)|(*c.bridge?0x100u:0u);
    }
    ProjectionLookup callback() {
        return [&](ra2map::Cell c) {
            const auto state=metadata(c);
            events.push_back({projection_key(c),static_cast<std::uint32_t>(*state.level),flags(state),0});
            return state;
        };
    }
    std::uint32_t digest(const CellProjectionResult& result) const {
        std::uint32_t value=2166136261u;
        const auto mix=[&](std::uint32_t word) { value=(value^word)*16777619u; };
        mix(projection_key(result.answer.value()));mix(result.steps);mix(result.lookups);
        for (const auto& event:events) for (auto word:event) mix(word);
        return value;
    }
};
}
