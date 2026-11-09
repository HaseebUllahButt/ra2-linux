#pragma once
#include "../gfx/palette.h"
#include "../rules/ini_file.h"
#include <algorithm>
#include <string>

namespace ra2::app {
// Base multiplayer picker order. Values come from the original [Colors] section.
inline gfx::Rgba house_color(const rules::IniFile& rules, int selected, float shade=1) {
    static constexpr const char* names[]={"Gold","DarkRed","DarkBlue","DarkGreen",
        "Orange","DarkSky","Purple","Magenta"};
    if(selected<0 || selected>7) throw std::runtime_error("Invalid base house colour");
    const auto values=rules.read_list("Colors",names[selected]);
    if(values.size()!=3) throw std::runtime_error("Missing original house colour");
    const float h=std::stoi(values[0])*6.0f/256;
    const float s=std::stoi(values[1])/255.0f;
    const float v=std::stoi(values[2])/255.0f*std::clamp(shade,0.0f,1.0f);
    const int sector=int(h); const float f=h-sector;
    const float a=v*(1-s), b=v*(1-s*f), c=v*(1-s*(1-f));
    float r=v,g=c,bl=a;
    switch(sector) {
    case 1:r=b;g=v;bl=a;break; case 2:r=a;g=v;bl=c;break;
    case 3:r=a;g=b;bl=v;break; case 4:r=c;g=a;bl=v;break;
    case 5:r=v;g=a;bl=b;break;
    }
    return {std::uint8_t(r*255),std::uint8_t(g*255),std::uint8_t(bl*255),255};
}
inline std::array<gfx::Rgba,16> house_ramp(const rules::IniFile& rules,int selected) {
    std::array<gfx::Rgba,16> result;
    // Playable approximation of the original colour-scheme shade ramp.
    for(unsigned i=0;i<16;++i) result[i]=house_color(rules,selected,1.0f-i/20.0f);
    return result;
}
}
