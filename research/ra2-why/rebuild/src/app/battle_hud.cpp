// Reference: Phobos-YRpp/SidebarClass.h (cameo progress and strips),
// Phobos-YRpp/RadarClass.h (RadarRect and RadarViewRect), and
// CnC_Red_Alert/CODE/RADAR.CPP (radar coordinate scaling).
// TODO-ORIGINAL: plain rectangles replace original sidebar/cameo artwork.
#include "battle_hud.h"
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace ra2::app {
namespace {
using render::Rectangle;
using Color = std::array<unsigned char, 4>;
constexpr Color ink{224, 230, 192, 255}, dark{22, 29, 32, 255},
    edge{104, 129, 126, 255}, green{70, 190, 90, 255}, red{220, 75, 60, 255},
    gold{255, 214, 64, 255};
bool valid(const render::Bitmap& b) {
    return b.width > 0 && b.height > 0 &&
        static_cast<std::uint64_t>(b.width)*b.height*4 <= b.rgba.size();
}
void pixel(render::Bitmap& b, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= b.width || y >= b.height) return;
    auto i = (static_cast<std::size_t>(y)*b.width+x)*4;
    std::copy(c.begin(), c.end(), b.rgba.begin()+i);
}
void fill(render::Bitmap& b, Rectangle r, Color c) {
    for (int y = std::max(0,r.y); y < std::min(b.height,r.y+r.height); ++y)
        for (int x = std::max(0,r.x); x < std::min(b.width,r.x+r.width); ++x) pixel(b,x,y,c);
}
void border(render::Bitmap& b, Rectangle r, Color c) {
    fill(b,{r.x,r.y,r.width,1},c); fill(b,{r.x,r.y+r.height-1,r.width,1},c);
    fill(b,{r.x,r.y,1,r.height},c); fill(b,{r.x+r.width-1,r.y,1,r.height},c);
}
// TODO-ORIGINAL: replace the embedded 3x5 font with original game typography.
void text(render::Bitmap& b, int x, int y, const std::string& s, Color c, int limit,
          int scale=1) {
    const std::string chars="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$:/-?.";
    const char* glyphs[]={"010101111101101","110101110101110","011100100100011",
        "110101101101110","111100110100111","111100110100100","011100101101011",
        "101101111101101","111010010010111","001001001101010","101101110101101",
        "100100100100111","101111111101101","101111111111101","010101101101010",
        "110101110100100","010101101111011","110101110101101","011100010001110",
        "111010010010010","101101101101111","101101101101010","101101111111101",
        "101101010101101","101101010010010","111001010100111","111101101101111",
        "010110010010111","110001010100111","110001010001110","101101111001001",
        "111100110001110","011100111101111","111001010010010","111101111101111",
        "111101111001110","011110010011110","000010000010000","001001010100100",
        "000000111000000","110001010000010","000000000000010"};
    int end=x+limit;
    for (unsigned char raw:s) {
        if (x+3*scale>end) break;
        auto n=chars.find(static_cast<char>(std::toupper(raw)));
        if(n!=std::string::npos) for(int j=0;j<15;++j)
            if(glyphs[n][j]=='1') fill(b,{x+(j%3)*scale,y+(j/3)*scale,scale,scale},c);
        x+=4*scale;
    }
}
bool inside(int x,int y,Rectangle r) {
    return x>=r.x && y>=r.y && x<r.x+r.width && y<r.y+r.height;
}
}
render::Rectangle BattleHud::build_button_bounds(int i) {
    if(i<0 || i>=build_columns*build_rows) return {};
    return {palette_x+i%build_columns*build_button_pitch_x,
        palette_y+i/build_columns*build_button_pitch_y,build_button_width,build_button_height};
}
render::Rectangle BattleHud::deploy_bounds() { return {palette_x,deploy_y,deploy_width,deploy_height}; }
render::Rectangle BattleHud::minimap_bounds() { return {minimap_x,minimap_y,minimap_region_width,minimap_region_height}; }
HudAction BattleHud::handle_click(int x,int y,const HudState& s) const {
    for(int i=0;i<static_cast<int>(s.choices.size()) && i<build_columns*build_rows;++i)
        if(inside(x,y,build_button_bounds(i)) && s.choices[i].enabled)
            return {HudActionKind::Build,s.choices[i].type,{}};
    if(inside(x,y,deploy_bounds())) return {HudActionKind::Deploy,{}, {}};
    auto r=minimap_bounds();
    if(inside(x,y,r) && s.map_width>0 && s.map_height>0)
        return {HudActionKind::Minimap,{},
            {static_cast<int>(static_cast<std::int64_t>(x-r.x)*s.map_width/r.width),
             static_cast<int>(static_cast<std::int64_t>(y-r.y)*s.map_height/r.height)}};
    return {};
}
void BattleHud::draw(render::Bitmap& b,const HudState& s) const {
    if(!valid(b)) return;
    fill(b,{sidebar_x,0,sidebar_width,view_height},dark);
    text(b,808,5,"CREDITS $"+std::to_string(s.cash),ink,212);
    text(b,808,15,"POWER "+std::to_string(s.power_output)+"/"+std::to_string(s.power_drain),ink,212);
    fill(b,{808,25,212,6},edge);
    const auto capacity=std::max({1,s.power_output,s.power_drain});
    fill(b,{808,25,static_cast<int>(212LL*std::max(0,s.power_output)/capacity),6},
         s.power_output<s.power_drain?red:green);
    text(b,808,34,"TODO-ORIGINAL",edge,212);
    for(int i=0;i<build_columns*build_rows;++i) {
        auto r=build_button_bounds(i); border(b,r,edge);
        if(i>=static_cast<int>(s.choices.size())) continue;
        const auto& c=s.choices[i]; auto color=c.enabled?ink:edge;
        text(b,r.x+4,r.y+6,c.name.empty()?c.type:c.name,color,r.width-8);
        text(b,r.x+4,r.y+19,"$"+std::to_string(c.cost),color,r.width-8);
        text(b,r.x+4,r.y+31,c.progress>=1000?"READY":"TODO-ORIGINAL",color,r.width-8);
        fill(b,{r.x+4,r.y+46,(r.width-8)*std::clamp(c.progress,0,1000)/1000,5},c.enabled?green:edge);
    }
    border(b,deploy_bounds(),ink); text(b,816,577,"DEPLOY",ink,190);
    int y=606;
    for(const auto& label:s.selected_labels) {
        if(y>642) break;
        text(b,808,y,label,ink,212); y+=8;
    }
    text(b,808,658,s.status,ink,212);
    auto r=minimap_bounds(); fill(b,r,{8,15,12,255});
    if(valid(s.minimap)) for(int y=0;y<r.height;++y) for(int x=0;x<r.width;++x) {
        const auto i=(static_cast<std::size_t>(y)*s.minimap.height/r.height*s.minimap.width+
            static_cast<std::size_t>(x)*s.minimap.width/r.width)*4;
        pixel(b,r.x+x,r.y+y,{s.minimap.rgba[i],s.minimap.rgba[i+1],s.minimap.rgba[i+2],s.minimap.rgba[i+3]});
    }
    if(s.map_width>0 && s.map_height>0 && s.camera.width>0 && s.camera.height>0) {
        auto scale=[](std::int64_t v,int map,int size) { return static_cast<int>(std::clamp<std::int64_t>(v,0,map)*size/map); };
        int x=scale(s.camera.x,s.map_width,r.width), y=scale(s.camera.y,s.map_height,r.height);
        int right=scale(static_cast<std::int64_t>(s.camera.x)+s.camera.width,s.map_width,r.width);
        int bottom=scale(static_cast<std::int64_t>(s.camera.y)+s.camera.height,s.map_height,r.height);
        if(right>x && bottom>y) border(b,{r.x+x,r.y+y,right-x,bottom-y},ink);
    }
    if(!s.result.empty()) {
        // Clear end banner: VICTORY green, DEFEAT red, DRAW gold.
        const bool win=s.result.find("VICT")!=std::string::npos;
        const Color tone=win?green:s.result.find("DEFEAT")!=std::string::npos?red:gold;
        const Rectangle box{(sidebar_x-604)/2,330,604,60};
        fill(b,box,dark); border(b,box,tone);
        // TODO-ORIGINAL: original end-of-match banner art and animation.
        const int scale=8, w=static_cast<int>(s.result.size())*4*scale-scale;
        text(b,box.x+std::max(0,(box.width-w)/2),box.y+(box.height-5*scale)/2,
             s.result,tone,box.width-16,scale);
    }
}
} // namespace ra2::app
