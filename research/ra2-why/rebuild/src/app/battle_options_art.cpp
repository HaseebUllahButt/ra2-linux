// Original art and drawing for the base in-game options menu.
#include "battle_options.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::app {
namespace {
std::vector<std::uint8_t> required(const fs::MixFileSystem& files, const char* name) {
    auto bytes=files.read_file(name);
    if(!bytes) throw std::runtime_error(std::string("Missing original options art: ")+name);
    return std::move(*bytes);
}
gfx::RgbaImage frame(const fs::MixFileSystem& files, const char* name, const gfx::Palette& pal, std::size_t i=0) {
    return gfx::ShpImage::parse(required(files,name)).decode_frame(i,pal);
}
void blit(render::Bitmap& dst, const gfx::RgbaImage& src, int x, int y) {
    for(int sy=0;sy<src.height;++sy) {
        const int dy=y+sy; if(dy<0||dy>=dst.height) continue;
        for(int sx=0;sx<src.width;++sx) {
            const int dx=x+sx; if(dx<0||dx>=dst.width) continue;
            const auto s=(std::size_t(sy)*src.width+sx)*4;
            if(!src.pixels[s+3]) continue;
            const auto d=(std::size_t(dy)*dst.width+dx)*4;
            std::copy_n(src.pixels.begin()+s,4,dst.rgba.begin()+d);
        }
    }
}
// Draws text through an RgbaImage view, then copies opaque pixels.
void text(render::Bitmap& dst, const ui::GameFont& font, const std::string& s, int x, int y, gfx::Rgba c) {
    const int w=int(font.text_width(s)), h=int(font.line_height());
    if(w<=0||h<=0) return;
    gfx::RgbaImage im{std::uint16_t(w),std::uint16_t(h),std::vector<std::uint8_t>(std::size_t(w)*h*4)};
    font.draw_text(im,s,0,0,c);
    blit(dst,im,x,y);
}
}

const char* side_archive(bool soviet) noexcept { return soviet?"SIDEC02.MIX":"SIDEC01.MIX"; }

BattleOptions BattleOptions::load(const std::vector<std::uint8_t>& exe, const ra2text::CsfFile& strings) {
    const auto* leave=strings.find("GUI:Leave");
    if(!leave) throw std::runtime_error("Missing UI text: GUI:Leave");
    return BattleOptions(ui::load_dialog_resource(exe,181,strings),ui::load_dialog_resource(exe,182,strings),leave->text);
}

BattleOptionsArt BattleOptionsArt::load(const fs::MixFileSystem& files) {
    BattleOptionsArt a;
    // Positions and palettes were matched pixel by pixel against the original
    // 1024x768 Esc screen (mean color error 0-5 per piece).
    const auto ui=gfx::Palette::from_bytes(required(files,"UIBKGD.PAL"));
    const auto side=gfx::Palette::from_bytes(required(files,"SIDEBAR.PAL"));
    a.background=frame(files,"BKGDLG.SHP",ui);
    a.credits=frame(files,"CREDITS.SHP",side);
    a.top=frame(files,"TOP.SHP",side);
    a.radar=frame(files,"RADAR.SHP",side);
    a.side1=frame(files,"SIDE1.SHP",side);
    a.side2=frame(files,"SIDE2B.SHP",side);
    a.side3=frame(files,"SIDE3.SHP",side);
    a.addon=frame(files,"ADDON.SHP",side);
    a.spacer=frame(files,"LSPACER.SHP",side);
    a.left_cap=frame(files,"LENDCAP.SHP",side,2);
    a.end_cap=frame(files,"RENDCAP.SHP",side);
    for(std::size_t i=0;i<3;++i) a.button[i]=frame(files,"SIDEBTTN.SHP",side,i);
    a.font=ui::GameFont::parse(required(files,"GAME.FNT"));
    if(a.background.width!=856||a.background.height!=736||a.button[0].width!=125||a.button[0].height!=25)
        throw std::runtime_error("Unsupported options art size");
    return a;
}

void draw_battle_options(render::Bitmap& f, const BattleOptions& menu, const BattleOptionsArt& art) {
    if(!menu.visible() || f.width!=BattleOptions::width || f.height!=BattleOptions::height ||
       f.rgba.size()<std::size_t(f.width)*f.height*4) return;
    blit(f,art.background,0,0);
    blit(f,art.spacer,0,736);
    blit(f,art.left_cap,800,736);
    blit(f,art.end_cap,828,736);
    const int x=BattleOptions::width-168;
    blit(f,art.credits,x,0);
    blit(f,art.top,x,16);
    blit(f,art.radar,x,48);
    blit(f,art.side1,x,158);
    for(int y=227;y<727;y+=50) blit(f,art.side2,x,y);
    blit(f,art.side3,x,727);
    blit(f,art.addon,x,753); // Only its top 15 rows fit on screen.
    if(menu.page()==OptionsPage::ConfirmAbort && !menu.prompt().empty()) {
        // TODO-ORIGINAL: dialog 182 places this centered static at (44,137,230,19)
        // dialog units; the in-game unit scale is not recovered. Center it.
        const int w=int(art.font.text_width(menu.prompt()));
        text(f,art.font,menu.prompt(),(856-w)/2,(736-int(art.font.line_height()))/2,{255,255,0,255});
    }
    for(const auto& b:menu.buttons()) {
        // TODO-ORIGINAL: SIDEBTTN.SHP frame 1 and 2 roles (hover, pressed) are inferred.
        blit(f,art.button[b.pressed?2:b.hovered?1:0],b.bounds.x,b.bounds.y);
        // Sampled from the original screen: enabled text is pure yellow;
        // disabled Load Game and Delete Game text is (0,81,115).
        const gfx::Rgba color=b.enabled?gfx::Rgba{255,255,0,255}:gfx::Rgba{0,81,115,255};
        const int w=int(art.font.text_width(b.label)), h=int(art.font.line_height());
        text(f,art.font,b.label,b.bounds.x+(b.bounds.width-w)/2,b.bounds.y+(b.bounds.height-h)/2,color);
    }
}
}
