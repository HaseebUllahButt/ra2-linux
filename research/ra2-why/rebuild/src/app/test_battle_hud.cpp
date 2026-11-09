#include "battle_hud.h"
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::app;
namespace {
render::Bitmap frame(int w=1024,int h=768) {
    return {w,h,render::Bytes(static_cast<std::size_t>(w)*h*4,17)};
}
bool same(const render::Bitmap& a,const render::Bitmap& b,render::Rectangle r) {
    for(int y=r.y;y<r.y+r.height;++y) for(int x=r.x;x<r.x+r.width;++x)
        for(int c=0;c<4;++c) if(a.rgba[(y*a.width+x)*4+c]!=b.rgba[(y*b.width+x)*4+c]) return false;
    return true;
}
}
int main() {
    BattleHud hud; HudState s;
    s.choices={{"E1","INFANTRY",100,true,500},{"MTNK","TANK",800,false,0}};
    auto r=BattleHud::build_button_bounds(0);
    assert(hud.handle_click(r.x,r.y,s).type=="E1");
    assert(hud.handle_click(r.x+r.width-1,r.y+r.height-1,s).kind==HudActionKind::Build);
    assert(hud.handle_click(r.x+r.width,r.y,s).kind==HudActionKind::None);
    assert(hud.handle_click(916,44,s).kind==HudActionKind::None);
    assert(hud.handle_click(r.x,r.y,s).kind==HudActionKind::None);
    hud.handle_click(808,44,s);
    assert(hud.handle_click(808,117,s).kind==HudActionKind::None);
    assert(hud.handle_click(808,560,s).kind==HudActionKind::Deploy);
    assert(hud.handle_click(808,600,s).kind==HudActionKind::None);
    assert(hud.handle_click(803,560,s).kind==HudActionKind::None);
    assert(hud.handle_click(804,672,s).kind==HudActionKind::None);
    s.map_width=440;s.map_height=192;
    auto a=hud.handle_click(914,720,s);
    assert(a.kind==HudActionKind::Minimap && a.position.x==220 && a.position.y==96);
    a=hud.handle_click(1023,767,s);assert(a.position.x==438 && a.position.y==190);
    assert(hud.handle_click(1024,720,s).kind==HudActionKind::None);
    assert(hud.handle_click(914,768,s).kind==HudActionKind::None);
    assert(BattleHud::build_button_bounds(16).width==0);
    s.choices.resize(17,{"EXTRA","EXTRA",1,true,0});
    assert(hud.handle_click(808,556,s).kind==HudActionKind::None);
    auto blank=frame(), b=blank;hud.draw(b,s);
    assert(same(blank,b,{0,0,804,768}));
    assert(!same(blank,b,{804,0,220,768}));
    auto changed=b;
    s.cash=12345;s.power_output=50;s.power_drain=100;
    s.selected_labels={"GI","MCV"};s.status="LOW POWER";s.choices[0].progress=1000;
    hud.draw(changed,s);
    for(auto region: {render::Rectangle{808,0,212,31},{808,606,212,40},{808,658,212,6},r})
        assert(!same(b,changed,region));
    s.minimap={2,1,{10,20,30,255,40,50,60,255}};
    hud.draw(b,s);
    assert(b.rgba[(700*1024+805)*4]==10);
    assert(b.rgba[(700*1024+1023)*4]==40);
    s.camera={110,48,220,96};hud.draw(changed,s);
    assert(changed.rgba[(696*1024+859)*4]==224);
    assert(!same(b,changed,{804,672,220,96}));
    s.result="VICTORY";hud.draw(changed,s);
    assert(!same(b,changed,{100,330,604,60}));
    // The banner is large, centered and color-coded: VICTORY green,
    // DEFEAT red, DRAW gold. It never paints into the sidebar.
    auto banner=frame();
    const auto count_rgb=[&](unsigned char r,unsigned char g,unsigned char bl) {
        int hits=0;
        for(int y=330;y<390;++y) for(int x=100;x<704;++x) {
            const auto i=(static_cast<std::size_t>(y)*1024+x)*4;
            if(banner.rgba[i]==r&&banner.rgba[i+1]==g&&banner.rgba[i+2]==bl) ++hits;
        }
        return hits;
    };
    hud.draw(banner,s);
    assert(count_rgb(70,190,90)>400);
    s.result="DEFEAT";hud.draw(banner,s);
    assert(count_rgb(220,75,60)>400 && count_rgb(70,190,90)==0);
    s.result="DRAW";hud.draw(banner,s);
    assert(count_rgb(255,214,64)>400 && count_rgb(220,75,60)==0);
    assert(banner.rgba[(360*1024+99)*4]==17 && banner.rgba[(360*1024+705)*4]==17);
    auto small=frame(810,50);hud.draw(small,s);
    assert(small.rgba.size()==810*50*4);
    render::Bitmap malformed{1024,768,{1,2,3}};hud.draw(malformed,s);
    assert(malformed.rgba.size()==3);
    s.minimap=malformed; hud.draw(changed,s);
    std::cout<<"battle HUD tests passed\n";
}
