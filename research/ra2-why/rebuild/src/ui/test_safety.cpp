#include "ui.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using Bytes=std::vector<std::uint8_t>;
static Bytes read(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("cannot open test input");
    return {std::istreambuf_iterator<char>(in),{}};
}
template<class F> static void rejects(F f) {
    bool caught=false; try { f(); } catch(const std::runtime_error&) { caught=true; } assert(caught);
}
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    auto exe=read(argv[1]), csf=read(argv[2]), pcx=read(argv[3]), fontbytes=read(argv[4]);
    ra2text::CsfFile text; text.parse(csf.data(),csf.size());
    auto font=ra2::ui::GameFont::parse(fontbytes);
    auto image=ra2::ui::decode_pcx(pcx);
    font.draw_text(image,"Single Player",-3,0,{255,255,255,255});
    auto main=ra2::ui::load_dialog(exe,ra2::ui::Page::Main,text);
    assert(main.hit_test(425,125,{1667})==1667);
    for(std::size_t n: {std::size_t(0),std::size_t(28),std::size_t(128),std::size_t(1024)}) {
        auto cut=exe; cut.resize(n); rejects([&]{ra2::ui::load_dialog(cut,ra2::ui::Page::Main,text);});
        auto cutpcx=pcx; cutpcx.resize(n); rejects([&]{ra2::ui::decode_pcx(cutpcx);});
        auto cutfont=fontbytes; cutfont.resize(n); rejects([&]{ra2::ui::GameFont::parse(cutfont);});
    }
    auto bad=fontbytes; bad[28+65*2]=0xff; bad[28+65*2+1]=0xff;
    rejects([&]{ra2::ui::GameFont::parse(bad);});
    auto badpcx=pcx; badpcx[128]=0xc0;
    rejects([&]{ra2::ui::decode_pcx(badpcx);});
    std::cout<<"UI bounds tests passed\n";
}
