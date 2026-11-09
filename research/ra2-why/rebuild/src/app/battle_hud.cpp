// Reference: Phobos-YRpp/SidebarClass.h (cameo progress and strips),
// Phobos-YRpp/RadarClass.h (RadarRect and RadarViewRect), and
// CnC_Red_Alert/CODE/RADAR.CPP (radar coordinate scaling).
// TODO-ORIGINAL: plain rectangles replace original sidebar/cameo artwork.
#include "battle_hud.h"
#include "../loop/profile.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../gfx/shp_decode.h"
#include <algorithm>
#include <cctype>
#include <cmath>
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
          int scale=2) {
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
void BattleHud::load_assets(const fs::MixFileSystem& files, const rules::IniFile& rules,
                            const rules::IniFile& art, const std::vector<BuildChoice>& choices) {
    panel_valid_=false;
    if(panel_.rgba.empty())panel_={view_width,minimap_y,render::Bytes(std::size_t(view_width)*minimap_y*4)};
    cameos_.clear();cameo_names_.clear();cameo_palette_.reset();
    cameo_files_=&files;
    const auto pal=files.read_file("cameo.pal");
    if(!pal)return;
    try { cameo_palette_=gfx::Palette::from_bytes(*pal); }
    catch(const std::exception&) { return; }
    for(const auto& c:choices) {
        const auto image=rules.read_string(c.type,"Image",c.type);
        cameo_names_[c.type]=art.read_string(image,"Cameo",image+"ICON")+".shp";
    }
}
const render::Bitmap* BattleHud::cameo(const std::string& type) const {
    auto cached=cameos_.find(type);
    if(cached!=cameos_.end())return valid(cached->second)?&cached->second:nullptr;
    if(!cameo_files_||!cameo_palette_)return nullptr;
    const auto name=cameo_names_.find(type);
    if(name==cameo_names_.end())return nullptr;
    render::Bitmap bitmap;
    const auto data=cameo_files_->read_file(name->second);
    if(data)try {
        auto decoded=gfx::ShpImage::parse(*data).decode_frame(0,*cameo_palette_);
        bitmap.width=decoded.width;bitmap.height=decoded.height;bitmap.rgba=std::move(decoded.pixels);
    } catch(const std::exception&) { /* TODO-ORIGINAL: fallback for unsupported cameo art. */ }
    const auto item=cameos_.emplace(type,std::move(bitmap)).first;
    return valid(item->second)?&item->second:nullptr;
}

render::Rectangle BattleHud::build_button_bounds(int i) {
    if(i<0 || i>=build_columns*build_rows) return {};
    return {palette_x+i%build_columns*build_button_pitch_x,
        palette_y+i/build_columns*build_button_pitch_y,build_button_width,build_button_height};
}
render::Rectangle BattleHud::deploy_bounds() { return {palette_x,deploy_y,deploy_width,deploy_height}; }
render::Rectangle BattleHud::minimap_bounds() { return {minimap_x,minimap_y,minimap_region_width,minimap_region_height}; }
HudAction BattleHud::handle_click(int x,int y,const HudState& s) const {
    if (y >= 36 && y < 58 && x >= 808 && x < 1018) {
        active_category_ = static_cast<BuildCategory>((x-808)/42);
        return {};
    }
    int slot=0;
    for (const auto& c : s.choices) {
        if (active_category_ != BuildCategory::All && c.category != active_category_) continue;
        if (slot >= build_columns*build_rows) break;
        if (inside(x,y,build_button_bounds(slot)) && c.enabled)
            return {HudActionKind::Build,c.type,{}};
        ++slot;
    }
    if(s.can_deploy && inside(x,y,deploy_bounds())) return {HudActionKind::Deploy,{}, {}};
    auto r=minimap_bounds();
    if(inside(x,y,r) && s.map_width>0 && s.map_height>0)
        return {HudActionKind::Minimap,{},
            {static_cast<int>(static_cast<std::int64_t>(x-r.x)*s.map_width/r.width),
             static_cast<int>(static_cast<std::int64_t>(y-r.y)*s.map_height/r.height)}};
    return {};
}
bool BattleHud::same_panel(const HudState& s) const {
    const auto& old=panel_state_;
    return panel_valid_&&panel_category_==active_category_&&old.cash==s.cash&&
        old.power_output==s.power_output&&old.power_drain==s.power_drain&&
        old.status==s.status&&old.result==s.result&&old.selected_labels==s.selected_labels&&
        old.can_deploy==s.can_deploy&&
        old.choices.size()==s.choices.size()&&std::equal(old.choices.begin(),old.choices.end(),s.choices.begin(),
            [](const auto& a,const auto& b) {
                return a.type==b.type&&a.name==b.name&&a.cost==b.cost&&a.enabled==b.enabled&&
                    a.progress==b.progress&&a.category==b.category&&a.queued==b.queued;
            });
}
void BattleHud::draw_panel(render::Bitmap& b,const HudState& s) const {
    fill(b,{sidebar_x,0,sidebar_width,minimap_y},dark);
    text(b,808,3,"CREDITS $"+std::to_string(s.cash),ink,212);
    text(b,808,17,"POWER OUT "+std::to_string(s.power_output)+" USE "+std::to_string(s.power_drain),
        s.power_output<s.power_drain?red:ink,212);
    fill(b,{808,29,212,6},s.power_output<s.power_drain?Color{75,30,24,255}:edge);
    const auto capacity=std::max({1,s.power_output,s.power_drain});
    fill(b,{808,29,static_cast<int>(212LL*std::max(0,s.power_output)/capacity),6},
         s.power_output<s.power_drain?red:green);
    // Enlarged tabs. The click test in handle_click uses these same rects.
    // Below the power bar (ends at y 35), above the buttons (start y 60).
    const char* tabs[] = {"ALL", "BLD", "INF", "VEH", "AIR"};
    for (int i=0;i<5;++i) {
        Rectangle tab{808+i*42,36,42,22};
        if(static_cast<int>(active_category_)==i) {
            // Clear active-tab mark: dim gold fill plus a gold frame.
            fill(b,{tab.x+1,tab.y+1,tab.width-2,tab.height-2},{58,50,18,255});
            border(b,tab,gold);
            border(b,{tab.x+1,tab.y+1,tab.width-2,tab.height-2},gold);
        } else {
            border(b,tab,edge);
        }
        std::string label=tabs[i];
        text(b,tab.x+(42-(4*static_cast<int>(label.size())-2))/2,tab.y+6,label,ink,40,2);
    }
    if (s.choices.empty() && s.result.empty()) {
        text(b,816,90,"DEPLOY YOUR",gold,200);
        text(b,816,106,"MCV FIRST",gold,200);
    }
    int i=0;
    for (const auto& c : s.choices) {
        if (active_category_ != BuildCategory::All && c.category != active_category_) continue;
        if (i>=build_columns*build_rows) break;
        auto r=build_button_bounds(i++); border(b,r,edge);
        const auto* image=cameo(c.type);
        if (image) {
            const auto& src=*image;
            for (int y=1;y<r.height-1;++y) for (int x=1;x<r.width-1;++x) {
                auto p=(static_cast<std::size_t>(y-1)*src.height/(r.height-2)*src.width+(x-1)*src.width/(r.width-2))*4;
                if (src.rgba[p+3]) pixel(b,r.x+x,r.y+y,{src.rgba[p],src.rgba[p+1],src.rgba[p+2],src.rgba[p+3]});
            }
        }
        auto color=c.enabled?ink:edge;
        const bool art_loaded=image!=nullptr;
        if (art_loaded) {
            // Original cameo art: no name, cost or BUILD text on top.
            if (c.progress>=1000) {
                text(b,r.x+32,r.y+22,"READY",gold,r.width-8,2);
            } else if (c.progress>0) {
                // Clock shading, like the original CLOCK.SHP overlay: a dark
                // wedge that grows clockwise from 12 o'clock.
                const double sweep=std::clamp(c.progress,0,1000)/1000.0*2*M_PI;
                const int cx=r.x+r.width/2, cy=r.y+r.height/2;
                const int rad=std::min(r.width,r.height)/2-2;
                // Local button coords; dx/dy measure from the button center.
                for (int y=1;y<r.height-1;++y)
                    for (int x=1;x<r.width-1;++x) {
                        const double dx=r.x+x+0.5-cx, dy=r.y+y+0.5-cy;
                        if (dx*dx+dy*dy>rad*rad) continue;
                        double ang=std::atan2(dx,-dy);
                        if (ang<0) ang+=2*M_PI;
                        if (ang<=sweep) {
                            // Blend toward black, like the original translucent
                            // clock table: keep 40 percent of the color.
                            auto i=(static_cast<std::size_t>(r.y+y)*b.width+r.x+x)*4;
                            b.rgba[i]=b.rgba[i]*2/5;
                            b.rgba[i+1]=b.rgba[i+1]*2/5;
                            b.rgba[i+2]=b.rgba[i+2]*2/5;
                        }
                    }
            }
        } else {
            // Fallback art: keep the text labels and the progress bar.
            text(b,r.x+4,r.y+6,c.name.empty()?c.type:c.name,color,r.width-8);
            text(b,r.x+4,r.y+19,"$"+std::to_string(c.cost),color,r.width-8);
            text(b,r.x+4,r.y+31,c.progress>=1000?"READY":c.progress>0?std::to_string(c.progress/10)+"%":"BUILD",color,r.width-8);
            fill(b,{r.x+4,r.y+46,(r.width-8)*std::clamp(c.progress,0,1000)/1000,5},c.enabled?green:edge);
        }
        if(c.queued>1) {  // Queue count badge: total items, active one included.
            const std::string n=std::to_string(c.queued);
            const int w=4*static_cast<int>(n.size())+3;
            fill(b,{r.x+r.width-w-2,r.y+r.height-9,w,7},dark);
            text(b,r.x+r.width-w-1,r.y+r.height-8,n,gold,214,1);
        }
    }
    if(s.can_deploy) {
        border(b,deploy_bounds(),ink); text(b,816,577,"DEPLOY",ink,190);
    }
    text(b,808,596,"SELECTED UNITS",edge,212);
    int y=606;
    for(const auto& label:s.selected_labels) {
        if(y>642) break;
        text(b,808,y,label,ink,212); y+=13;
    }
    // Status on two short rows so long text does not cut off. Rows sit
    // below the selected list (ends y 643) and above the minimap (y 672).
    const std::string& st=s.status;
    std::string row1=st,row2;
    if(st.size()>26) {
        auto cut=st.rfind(' ',26);
        if(cut==std::string::npos) cut=26;
        row1=st.substr(0,cut);
        row2=st.substr(cut+(st[cut]==' '?1:0));
    }
    text(b,808,650,row1,ink,212);
    text(b,808,662,row2,ink,212);
}
void BattleHud::draw(render::Bitmap& b,const HudState& s) const {
    loop::profile::Timer probe(loop::profile::Phase::HudDraw);
    if(!valid(b))return;
    if(!same_panel(s)) {
        panel_valid_=false;
        if(panel_.rgba.empty())panel_={view_width,minimap_y,render::Bytes(std::size_t(view_width)*minimap_y*4)};
        draw_panel(panel_,s);
        panel_state_.cash=s.cash;panel_state_.power_output=s.power_output;panel_state_.power_drain=s.power_drain;
        panel_state_.selected_labels=s.selected_labels;panel_state_.choices=s.choices;
        panel_state_.status=s.status;panel_state_.result=s.result;panel_state_.can_deploy=s.can_deploy;
        panel_category_=active_category_;panel_valid_=true;
    }
    const int right=std::min(b.width,view_width),bottom=std::min(b.height,minimap_y);
    if(right>sidebar_x)for(int y=0;y<bottom;++y)
        std::copy_n(panel_.rgba.begin()+(std::size_t(y)*view_width+sidebar_x)*4,
            std::size_t(right-sidebar_x)*4,b.rgba.begin()+(std::size_t(y)*b.width+sidebar_x)*4);
    // Hover tooltip: the hovered cameo cost, drawn over the cached panel.
    if(s.pointer_x>=palette_x&&s.pointer_x<view_width&&s.pointer_y>=palette_y&&s.pointer_y<deploy_y) {
        int slot=0;
        for(const auto& c:s.choices) {
            if(active_category_!=BuildCategory::All&&c.category!=active_category_) continue;
            if(slot>=build_columns*build_rows) break;
            auto r=build_button_bounds(slot++);
            if(c.enabled&&inside(s.pointer_x,s.pointer_y,r)) {
                const std::string tip="$"+std::to_string(c.cost);
                const int w=4*static_cast<int>(tip.size())+6;
                const Rectangle box{r.x+r.width-w,r.y+r.height+1,w,11};
                fill(b,box,dark); border(b,box,edge);
                text(b,box.x+3,box.y+3,tip,gold,w-4,1);
                break;
            }
        }
    }
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
