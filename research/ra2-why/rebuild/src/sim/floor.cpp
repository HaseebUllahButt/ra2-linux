#include "floor.h"
#include <algorithm>
#include <stdexcept>
namespace ra2::sim {
int original_floor_height(std::int8_t level,std::uint8_t ramp,std::int32_t world_x,std::int32_t world_y) {
    // Original 20 planes at 0081C900, after 0047B3A0's local static initialization.
    // Coefficients are exact multiples of LevelHeight/256; integer arithmetic is equivalent here.
    struct Plane { int x,y,b,cap,extra; };
    constexpr Plane planes[]={
        {1,0,0,104,0},{0,1,0,104,0},{-1,0,104,104,0},{0,-1,104,104,0},
        {1,1,-104,104,0},{-1,1,0,104,0},{-1,-1,104,104,0},{1,-1,0,104,0},
        {1,1,0,104,0},{-1,1,104,104,0},{-1,-1,208,104,0},{1,-1,104,104,0},
        {1,1,0,208,0},{-1,1,104,208,0},{-1,-1,208,208,0},{1,-1,104,208,0},
        {0,0,0,52,52},{0,0,104,52,-52},{0,0,0,52,52},{0,0,104,52,-52}};
    if (ramp>20) throw std::out_of_range("0047B3A0 ramp index exceeds the recovered plane table");
    // Original first truncates signed level*104+0.5 before adding ramp height.
    const int base=static_cast<int>(level)*104+(level<0?1:0);
    if (!ramp) return base;
    const auto& p=planes[ramp-1];
    const auto x=static_cast<std::uint32_t>(world_x)&255u;
    const auto y=static_cast<std::uint32_t>(world_y)&255u;
    const int numerator=104*(p.x*static_cast<int>(x)+p.y*static_cast<int>(y))+(p.b+p.extra)*256;
    const int bounded=std::clamp(numerator,0,p.cap*256);
    return (base*256+bounded)/256;
}
}
