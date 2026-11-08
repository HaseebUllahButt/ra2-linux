#include "ui.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
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
static void check_blit(const gfx::RgbaImage& output, const gfx::RgbaImage& source, int x, int y) {
    std::size_t checked=0;
    for (int sy=0;sy<source.height && y+sy<output.height;++sy)
        for (int sx=0;sx<source.width && x+sx<output.width;++sx) {
            auto s=(static_cast<std::size_t>(sy)*source.width+sx)*4;
            auto d=(static_cast<std::size_t>(y+sy)*output.width+x+sx)*4;
            if(source.pixels[s+3]) {
                assert(std::equal(source.pixels.begin()+s,source.pixels.begin()+s+4,output.pixels.begin()+d));
                ++checked;
            }
        }
    assert(checked>0);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    fs::MixFileSystem files(argv[1]); files.bootstrap(); files.mount("langmd.mix");
    // The original shell mounts these archives at 0x00534E50.
    files.mount("ntrlmd.mix"); files.mount("neutral.mix");
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
    auto shell=ui::draw_shell_background(files,800,600);
    assert(shell.width==800 && shell.height==600 && shell.pixels.size()==800*600*4);
    auto palbytes=files.read_file("SHELL.PAL"); auto bgbytes=files.read_file("MNSCRNL.SHP");
    auto btnbytes=files.read_file("SDBTNANM.SHP"); auto bottombytes=files.read_file("SDBTM.SHP");
    assert(palbytes && bgbytes && btnbytes && bottombytes);
    auto pal=gfx::Palette::from_bytes(*palbytes);
    auto bg=gfx::ShpImage::parse(*bgbytes).decode_frame(0,pal);
    auto buttonpal=files.read_file("SDBTNANM.PAL"); assert(buttonpal);
    auto cell=gfx::ShpImage::parse(*btnbytes).decode_frame(10,gfx::Palette::from_bytes(*buttonpal));
    auto bottom=gfx::ShpImage::parse(*bottombytes).decode_frame(0,pal);
    check_blit(shell,bg,0,0); check_blit(shell,cell,644,199);
    check_blit(shell,cell,644,535); check_blit(shell,bottom,632,577);
    ui::MenuNavigation nav;
    assert(nav.activate(1559)==ui::Action::None && nav.page()==ui::Page::Main);
    assert(nav.activate(1667)==ui::Action::PageChanged && nav.page()==ui::Page::SinglePlayer);
    assert(nav.activate(1401)==ui::Action::PageChanged && nav.page()==ui::Page::Skirmish);
    assert(nav.activate(1559)==ui::Action::None && nav.page()==ui::Page::Skirmish);
    assert(nav.activate(1472)==ui::Action::PageChanged && nav.page()==ui::Page::SinglePlayer);
    assert(nav.activate(1670)==ui::Action::PageChanged && nav.page()==ui::Page::Main);
    assert(nav.activate(1006)==ui::Action::None && nav.page()==ui::Page::Main);
    fs::MixFileSystem base(argv[1]);
    base.bootstrap(fs::GameVariant::RedAlert2); base.mount_language(fs::GameVariant::RedAlert2);
    base.mount("neutral.mix");
    base.mount("multi.mix"); files.mount("multimd.mix");
    auto basecsf=base.read_file("ra2.csf"); assert(basecsf);
    auto base_art=base.read_file("art.ini"); assert(base_art && !base_art->empty());
    rules::IniFile art; art.parse(std::string_view(reinterpret_cast<const char*>(base_art->data()),base_art->size()));
    assert(art.section_count()>100);
    ra2text::CsfFile base_strings; base_strings.parse(basecsf->data(),basecsf->size());
    auto base_exe=read(std::string(argv[1])+"/game.exe");
    auto base_main=ui::load_dialog(base_exe,ui::Page::Main,base_strings);
    auto base_single=ui::load_dialog(base_exe,ui::Page::SinglePlayer,base_strings);
    auto base_skirmish=ui::load_dialog(base_exe,ui::Page::Skirmish,base_strings);
    assert(base_main.controls.size()==11 && base_single.controls.size()==8 && base_skirmish.controls.size()==51);
    for (auto id : ui::SkirmishControls::opponents)
        assert(std::any_of(base_skirmish.controls.begin(),base_skirmish.controls.end(),[id](const auto& c){return c.id==id;}));
    for (auto id : ui::SkirmishControls::countries)
        assert(std::any_of(base_skirmish.controls.begin(),base_skirmish.controls.end(),[id](const auto& c){return c.id==id;}));
    for (auto id : ui::SkirmishControls::colors)
        assert(std::any_of(base_skirmish.controls.begin(),base_skirmish.controls.end(),[id](const auto& c){return c.id==id;}));
    auto base_shell=ui::draw_shell_background(base,800,600);
    assert(base_shell.width==800 && base_shell.height==600);
    auto base_maps=ui::load_skirmish_maps(base,base_strings);
    auto expansion_maps=ui::load_skirmish_maps(files,strings);
    assert(base_maps.size()>=137 && expansion_maps.size()>=161);
    assert(base_maps[0].id=="MP02T2" && base_maps[0].filename=="MP02T2.MAP" && !base_maps[0].name.empty());
    assert(expansion_maps[0].id=="XMP29U2" && !expansion_maps[0].name.empty());
    assert(std::any_of(base_maps.begin(),base_maps.end(),[](const auto& map){return map.available;}));
    assert(std::any_of(expansion_maps.begin(),expansion_maps.end(),[](const auto& map){return map.available;}));
    assert(std::any_of(base_maps.begin(),base_maps.end(),[](const auto& map){
        return map.source_archive=="Grinder.mmx" && map.filename=="Grinder.MAP" && map.available && !map.name.empty();
    }));
    assert(std::any_of(expansion_maps.begin(),expansion_maps.end(),[](const auto& map){
        return map.source_archive=="DeepFrze.yro" && map.available && map.description_key=="Name:DpFreeze";
    }));
    auto menu=ui::Menu::load(base,base_exe,base_strings);
    rules::IniFile defaults;
    auto empty_setup=ui::read_skirmish_setup(defaults);
    assert(empty_setup.scenario_index==0 && empty_setup.credits==10000 && empty_setup.game_speed==6);
    assert(empty_setup.short_game==false && empty_setup.super_weapons==true);
    assert(empty_setup.opponents[0].opponent==ui::Opponent::Hard);
    assert(empty_setup.opponents[1].opponent==ui::Opponent::Closed);
    rules::IniFile defaults_rules; defaults_rules.parse("[MultiplayerDialogSettings]\nMoney=7000\nMinMoney=1000\nMoneyIncrement=250\nShortGame=yes\n");
    auto rules_setup=ui::read_skirmish_setup(defaults,&defaults_rules);
    assert(rules_setup.credits==7000 && rules_setup.short_game==true);
    assert(rules_setup.min_credits==1000 && rules_setup.max_credits==10000 && rules_setup.credit_increment==250);
    assert(ui::credit_from_slider_pointer(6,128,5000,10000,100)==5000);
    assert(ui::credit_from_slider_pointer(38,128,5000,10000,100)==7400);
    assert(ui::credit_from_slider_pointer(71,128,5000,10000,100)==10000);
    assert(!ui::credit_from_slider_pointer(5,63,5000,10000,100));
    assert(!ui::credit_from_slider_pointer(5,128,5000,10000,0));
    rules::IniFile saved; saved.parse("[MultiPlayer]\nSide=Russians\nColor=2\n[Skirmish]\nCredits=10000\nSlot01=4,-2,-2\n");
    auto setup=ui::read_skirmish_setup(saved);
    assert(setup.country=="Russians" && setup.color==2 && setup.credits==10000);
    assert(setup.opponents[0].opponent==ui::Opponent::Easy && setup.opponents[0].country==-2);
    auto actual=rules::IniFile::from_file(std::string(argv[1])+"/RA2.INI");
    assert(menu.skirmish_setup().credits==actual.read_integer("Skirmish","Credits"));
    // Reference dialog normalization at 0x00775BC0, followed by cell snapping.
    for (int dlu : {125,152,179,206,233,122,149,176}) {
        const int expected=(dlu*163/100-199+21)/42;
        for (int font_height=8;font_height<=24;++font_height) {
            const int reference=(100*font_height+4)/8;
            const int initial=(dlu*font_height+4)/8;
            assert((initial*163/reference-199+21)/42==expected);
        }
    }
    auto unhovered=menu.render();
    bool heading_changed=false;
    for (int y=8;y<25;++y) for(int x=632;x<800;++x) {
        auto p=(static_cast<std::size_t>(y)*800+x)*4;
        if (!std::equal(unhovered.pixels.begin()+p,unhovered.pixels.begin()+p+4,
                        base_shell.pixels.begin()+p)) heading_changed=true;
    }
    assert(heading_changed);
    menu.pointer_down(645,536); assert(menu.pointer_up(645,536)==ui::Action::None);
    assert(menu.confirming_exit());
    auto confirm=menu.render(); assert(confirm.pixels!=unhovered.pixels);
    menu.pointer_down(487,423); assert(menu.pointer_up(487,423)==ui::Action::None);
    assert(!menu.confirming_exit() && menu.page()==ui::Page::Main);
    menu.pointer_down(645,536); assert(menu.pointer_up(645,536)==ui::Action::None);
    assert(menu.key_down(ui::Key::Escape)==ui::Action::None && !menu.confirming_exit());
    menu.pointer_down(645,536); assert(menu.pointer_up(645,536)==ui::Action::None);
    menu.pointer_down(487,390); assert(menu.pointer_up(487,390)==ui::Action::Quit);
    assert(!menu.confirming_exit());
    menu.pointer_move(644,199);
    auto hovered=menu.render(); assert(hovered.pixels!=unhovered.pixels);
    menu.pointer_down(644,199);
    auto pressed=menu.render(); assert(pressed.pixels!=hovered.pixels);
    assert(menu.pointer_up(643,199)==ui::Action::None && menu.page()==ui::Page::Main);
    menu.pointer_down(800,199); assert(menu.pointer_up(800,199)==ui::Action::None);
    menu.pointer_down(645,200); assert(menu.pointer_up(645,200)==ui::Action::PageChanged);
    assert(menu.page()==ui::Page::SinglePlayer); menu.render();
    menu.pointer_down(645,284); assert(menu.pointer_up(645,284)==ui::Action::PageChanged);
    assert(menu.page()==ui::Page::Skirmish); auto skirmish_image=menu.render();
    auto speed_label=std::find_if(base_skirmish.controls.begin(),base_skirmish.controls.end(),
                                  [](const auto& c){return c.id==1689;});
    assert(speed_label!=base_skirmish.controls.end());
    const int sx=(speed_label->bounds.x*3+1)/2, sy=(speed_label->bounds.y*163+50)/100;
    bool label_pixel=false;
    for(int y=sy;y<sy+16;++y) for(int x=sx;x<sx+80;++x) {
        auto p=(static_cast<std::size_t>(y)*800+x)*4;
        if(skirmish_image.pixels[p]==255 && skirmish_image.pixels[p+1]==255 && skirmish_image.pixels[p+2]==0)
            label_pixel=true;
    }
    assert(label_pixel);
    auto credit_control=std::find_if(base_skirmish.controls.begin(),base_skirmish.controls.end(),
                                     [](const auto& c){return c.id==ui::SkirmishControls::credits;});
    assert(credit_control!=base_skirmish.controls.end());
    const int credit_x=(credit_control->bounds.x*3+1)/2;
    const int credit_y=(credit_control->bounds.y*163+50)/100;
    const int credit_width=(credit_control->bounds.width*3+1)/2+1;
    const int left_value=*ui::credit_from_slider_pointer(6,credit_width,menu.setup().min_credits,
                                                        menu.setup().max_credits,menu.setup().credit_increment);
    menu.pointer_down(credit_x+6,credit_y+10);
    menu.pointer_up(credit_x+6,credit_y+10);
    assert(menu.setup().credits==left_value);
    menu.pointer_down(credit_x+6,credit_y+10);
    menu.pointer_move(credit_x+credit_width+100,credit_y+10);
    assert(menu.pointer_up(credit_x+credit_width+100,credit_y+10)==ui::Action::SetupChanged);
    assert(menu.setup().credits==menu.setup().max_credits);
    const auto previous_short_game=menu.setup().short_game; assert(previous_short_game);
    auto short_control=std::find_if(base_skirmish.controls.begin(),base_skirmish.controls.end(),
                                    [](const auto& c){return c.id==1358;});
    assert(short_control!=base_skirmish.controls.end());
    const int short_x=(short_control->bounds.x*3+1)/2;
    const int short_y=(short_control->bounds.y*163+50)/100;
    menu.pointer_down(short_x,short_y); assert(menu.pointer_up(short_x,short_y)==ui::Action::SetupChanged);
    assert(menu.setup().short_game && *menu.setup().short_game!=*previous_short_game);
    menu.pointer_down(short_x,short_y); assert(menu.pointer_up(short_x,short_y)==ui::Action::SetupChanged);
    assert(menu.setup().short_game==previous_short_game);
    auto ally=std::find_if(base_skirmish.controls.begin(),base_skirmish.controls.end(),
                           [](const auto& c){return c.id==1693;});
    if (ally!=base_skirmish.controls.end() && (ally->style&0x08000000u)) {
        const int x=(ally->bounds.x*3+1)/2, y=(ally->bounds.y*163+50)/100;
        menu.pointer_down(x,y); assert(menu.pointer_up(x,y)==ui::Action::None);
    }
    menu.pointer_down(645,242); assert(menu.pointer_up(645,242)==ui::Action::None);
    menu.pointer_down(645,284); assert(menu.pointer_up(645,284)==ui::Action::None);
    menu.pointer_down(645,536); assert(menu.pointer_up(645,536)==ui::Action::PageChanged);
    assert(menu.page()==ui::Page::SinglePlayer);
    auto expansion_menu=ui::Menu::load(files,exe,strings);
    assert(expansion_menu.render().width==800);
    fails([&]{ui::draw_shell_background(files,640,480);});
    auto damaged=*title;damaged.resize(140); fails([&]{ui::decode_pcx(damaged);});
    damaged=*title;damaged[0]=0;fails([&]{ui::decode_pcx(damaged);});
    auto broken=exe;broken.resize(128);fails([&]{ui::load_dialog(broken,ui::Page::Main,strings);});
    std::cout<<"original dialogs, labels, hit bounds, title PCX and shell background passed\n";
}
