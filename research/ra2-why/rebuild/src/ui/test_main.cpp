#include "ui.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace ra2;
static std::vector<std::uint8_t> read(const std::string& path) {
    std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("cannot read file");
    return {std::istreambuf_iterator<char>(f),{}};
}
template<class F> void fails(F f) {bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}assert(caught);}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    fs::MixFileSystem files(argv[1]); files.bootstrap(); files.mount("langmd.mix");
    auto csf=files.read_file("ra2md.csf"), title=files.read_file("TITLE.PCX");
    auto fontdata=files.read_file("GAME.FNT"); assert(csf && title && fontdata);
    ra2text::CsfFile strings;strings.parse(csf->data(),csf->size());
    auto exe=read(std::string(argv[1])+"/gamemd.exe");
    auto main=ui::load_dialog(exe,ui::Page::Main,strings);
    assert(main.resource_id==226 && main.bounds.width==533 && main.bounds.height==369 && main.controls.size()==11);
    auto button=std::find_if(main.controls.begin(),main.controls.end(),[](const auto& c){return c.id==1667;});
    assert(button!=main.controls.end() && button->label_key=="GUI:SinglePlayer" && button->label=="Single Player");
    auto pixel=ui::dialog_to_pixels({4,8,12,16},8,16);
    assert(pixel.x==8 && pixel.y==16 && pixel.width==24 && pixel.height==32);
    fails([&]{ui::dialog_to_pixels({0,0,1,1},0,16);});
    assert(button->bounds.x==425 && button->bounds.y==125 && button->bounds.width==108 && button->bounds.height==23);
    assert(main.hit_test(425,125,{1667})==1667 && !main.hit_test(425,125,{}) && !main.hit_test(533,125,{1667}));
    assert(main.hit_test_pixels(850,250,8,16,{1667})==1667);
    assert(!main.hit_test_pixels(1066,250,8,16,{1667}));
    auto single=ui::load_dialog(exe,ui::Page::SinglePlayer,strings);assert(single.resource_id==256 && single.controls.size()==8);
    auto skirmish=ui::load_dialog(exe,ui::Page::Skirmish,strings);assert(skirmish.resource_id==258 && skirmish.controls.size()==72);
    auto start=std::find_if(skirmish.controls.begin(),skirmish.controls.end(),[](const auto& c){return c.id==1559;});
    assert(start!=skirmish.controls.end() && start->label=="Start Game" && start->bounds.x==425 && start->bounds.y==149);
    assert(!skirmish.hit_test(425,149,{}) && skirmish.hit_test(425,149,{1559})==1559);
    auto font=ui::GameFont::parse(*fontdata);
    auto letter=font.glyph(u'A',{255,0,0,255}); assert(letter && letter->width==7 && letter->height==16 && font.line_height()==17);
    assert(std::any_of(letter->pixels.begin(),letter->pixels.end(),[](auto v){return v!=0;}));
    gfx::RgbaImage canvas{100,20,std::vector<std::uint8_t>(100*20*4)};
    font.draw_text(canvas,"Single Player",-2,0,{255,0,0,255});
    assert(std::any_of(canvas.pixels.begin(),canvas.pixels.end(),[](auto v){return v!=0;}));
    fails([&]{font.draw_text(canvas,"\xff",0,0,{255,0,0,255});});
    auto invalid_font=*fontdata; invalid_font.resize(50); fails([&]{ui::GameFont::parse(invalid_font);});
    auto img=ui::decode_pcx(*title); assert(img.width==640 && img.height==480 && img.pixels.size()==640*480*4);
    auto damaged=*title;damaged.resize(140); fails([&]{ui::decode_pcx(damaged);});
    damaged=*title;damaged[0]=0;fails([&]{ui::decode_pcx(damaged);});
    auto broken=exe;broken.resize(128);fails([&]{ui::load_dialog(broken,ui::Page::Main,strings);});
    std::cout<<"original dialogs, labels, hit bounds and title PCX passed\n";
}
