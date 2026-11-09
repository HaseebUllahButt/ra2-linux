#include "battle_hud.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../gfx/shp_decode.h"
#include <cassert>
#include <algorithm>
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
int main(int argc, char** argv) {
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
    HudState tabs;
    tabs.choices={{"GAPOWR","POWER",800,true,0,BuildCategory::Buildings},
                  {"E1","GI",200,true,0,BuildCategory::Infantry},
                  {"MTNK","TANK",700,true,0,BuildCategory::Vehicles}};
    hud.handle_click(808+2*42,44,tabs);
    assert(hud.handle_click(r.x+1,r.y+1,tabs).type=="E1");
    hud.handle_click(808+3*42,44,tabs);
    assert(hud.handle_click(r.x+1,r.y+1,tabs).type=="MTNK");
    hud.handle_click(808,44,tabs);
    // Enlarged tabs: the drawn tab and the hit area are the same rect.
    // Tabs sit at y 36..58, below the power bar, above the buttons.
    assert(hud.handle_click(808,35,tabs).kind==HudActionKind::None);
    assert(hud.handle_click(808,58,tabs).kind==HudActionKind::None);
    assert(hud.handle_click(808,57,tabs).kind==HudActionKind::None);
    hud.handle_click(809,37,tabs);
    assert(hud.handle_click(r.x+1,r.y+1,tabs).type=="GAPOWR");
    auto tabframe=frame(); hud.draw(tabframe,tabs);
    assert(tabframe.rgba[(36*1024+809)*4]!=17); // top border row of the first tab
    assert(tabframe.rgba[(57*1024+809)*4]!=17); // bottom border row
    assert(tabframe.rgba[(35*1024+809)*4]==22); // power bar row: plain sidebar dark
    assert(tabframe.rgba[(58*1024+809)*4]==22); // gap above the buttons
    if (argc > 1) {
        fs::MixFileSystem files(argv[1]);
        files.mount_language(fs::GameVariant::RedAlert2);
        files.bootstrap(fs::GameVariant::RedAlert2);
        files.mount_overlays(); files.mount_resources();
        auto read_ini=[&](const char* name) {
            auto bytes=files.read_file(name); assert(bytes);
            rules::IniFile ini;
            ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
            return ini;
        };
        auto rules=read_ini("rules.ini"), art=read_ini("art.ini");
        auto palette_bytes=files.read_file("cameo.pal"); assert(palette_bytes);
        auto palette=gfx::Palette::from_bytes(*palette_bytes);
        HudState originals;
        originals.choices={{"GAPOWR","",800,true,0},{"GAPILE","",500,true,0},
                           {"MTNK","",700,true,0}};
        hud.load_assets(files,rules,art,originals.choices);
        auto screen=frame(); hud.draw(screen,originals);
        int i=0;
        for (const char* name : {"POWRICON.SHP","BRRKICON.SHP","GTNKICON.SHP"}) {
            auto bytes=files.read_file(name); assert(bytes);
            auto image=gfx::ShpImage::parse(*bytes).decode_frame(0,palette);
            auto box=BattleHud::build_button_bounds(i++);
            int matches=0;
            // This strip is clear of the text and progress indicators.
            for (int y=42;y<46;++y) for (int x=1;x<box.width-1;++x) {
                auto source=(static_cast<std::size_t>(y-1)*image.height/(box.height-2)*image.width+
                             (x-1)*image.width/(box.width-2))*4;
                auto dest=(static_cast<std::size_t>(box.y+y)*screen.width+box.x+x)*4;
                if (image.pixels[source+3] &&
                    std::equal(image.pixels.begin()+source,image.pixels.begin()+source+4,
                               screen.rgba.begin()+dest)) ++matches;
            }
            assert(matches>100);
            // Loaded cameo art carries no idle text: the whole opaque
            // interior matches the source art exactly at progress 0.
            int exact=0, total=0;
            for (int y=1;y<box.height-1;++y) for (int x=1;x<box.width-1;++x) {
                auto source=(static_cast<std::size_t>(y-1)*image.height/(box.height-2)*image.width+
                             (x-1)*image.width/(box.width-2))*4;
                if (!image.pixels[source+3]) continue;
                auto dest=(static_cast<std::size_t>(box.y+y)*screen.width+box.x+x)*4;
                ++total;
                if (std::equal(image.pixels.begin()+source,image.pixels.begin()+source+4,
                               screen.rgba.begin()+dest)) ++exact;
            }
            assert(total>1000 && exact==total);
        // Clock checks compare button 0 art with this icon, so run them
        // only for the first icon (POWRICON, i is already incremented).
        if (i!=1) continue;
        // READY shows clearly in gold over loaded art.
        originals.choices[0].progress=1000;
        hud.draw(screen,originals);
        auto ready_box=BattleHud::build_button_bounds(0);
        int gold_hits=0;
        for (int y=ready_box.y;y<ready_box.y+ready_box.height;++y)
            for (int x=ready_box.x;x<ready_box.x+ready_box.width;++x) {
                auto i=(static_cast<std::size_t>(y)*screen.width+x)*4;
                if (screen.rgba[i]==255&&screen.rgba[i+1]==214&&screen.rgba[i+2]==64) ++gold_hits;
            }
        assert(gold_hits>50);
        // Clock shading: progress 250 darkens a wedge clockwise from the
        // top. Top-right is inside the wedge, top-left is not.
        originals.choices[0].progress=250;
        hud.draw(screen,originals);
        auto clock_box=BattleHud::build_button_bounds(0);
        // The clock wedge only covers a circle around the button center.
        const double clock_rad=std::min(clock_box.width,clock_box.height)/2.0-2;
        auto in_circle=[&](int x,int y) {
            double dx=x+0.5-clock_box.width/2.0, dy=y+0.5-clock_box.height/2.0;
            return dx*dx+dy*dy<=clock_rad*clock_rad;
        };
        auto art_pixel=[&](int bx,int by) {
            auto source=(static_cast<std::size_t>(by-1)*image.height/(clock_box.height-2)*image.width+
                         (bx-1)*image.width/(clock_box.width-2))*4;
            return source;
        };
        int in_wedge=0, out_wedge=0;
        for (int y=2;y<clock_box.height/2;++y) for (int x=clock_box.width/2;x<clock_box.width-2;++x) {
            auto source=art_pixel(x,y);
            if (!image.pixels[source+3] || !in_circle(x,y)) continue;
            auto dest=(static_cast<std::size_t>(clock_box.y+y)*screen.width+clock_box.x+x)*4;
            // Shaded pixels are exactly half the art color; skip very dark
            // art where half and full are too close to tell apart.
            if (image.pixels[source]>60 && screen.rgba[dest]==image.pixels[source]/2) ++in_wedge;
        }
        for (int y=2;y<clock_box.height/2;++y) for (int x=2;x<clock_box.width/2;++x) {
            auto source=art_pixel(x,y);
            if (!image.pixels[source+3] || !in_circle(x,y)) continue;
            auto dest=(static_cast<std::size_t>(clock_box.y+y)*screen.width+clock_box.x+x)*4;
            if (std::equal(image.pixels.begin()+source,image.pixels.begin()+source+4,
                           screen.rgba.begin()+dest)) ++out_wedge;
        }
        assert(in_wedge>50 && in_wedge+out_wedge>200);
        // Progress 750: the wedge covers 270 degrees. Top-left is the only
        // clean quadrant; top-right and bottom-left are darkened.
        originals.choices[0].progress=750;
        hud.draw(screen,originals);
        int dark_tl=0, opaque_tl=0, dark_mid=0, opaque_mid=0;
        // Top-left quarter-disc slice, clear of the 270 degree boundary.
        for (int y=8;y<24;++y) for (int x=32;x<48;++x) {
            auto source=art_pixel(x,y);
            if (!image.pixels[source+3] || !in_circle(x,y)) continue;
            auto dest=(static_cast<std::size_t>(clock_box.y+y)*screen.width+clock_box.x+x)*4;
            ++opaque_tl;
            if (image.pixels[source]>60 && screen.rgba[dest]==image.pixels[source]/2) ++dark_tl;
        }
        for (int y=32;y<48;++y) for (int x=32;x<48;++x) {
            auto source=art_pixel(x,y);
            if (!image.pixels[source+3] || !in_circle(x,y)) continue;
            auto dest=(static_cast<std::size_t>(clock_box.y+y)*screen.width+clock_box.x+x)*4;
            ++opaque_mid;
            if (image.pixels[source]>60 && screen.rgba[dest]==image.pixels[source]/2) ++dark_mid;
        }
        assert(dark_tl==0 && opaque_tl>10);
        assert(dark_mid>50 && dark_mid<=opaque_mid);
        originals.choices[0].progress=0;
        }
    }
    std::cout<<"battle HUD tests passed\n";
}
