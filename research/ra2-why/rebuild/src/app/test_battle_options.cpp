// Options menu checks. Pass a base game directory with game.exe to also
// check the original dialogs and art.
#include "battle_options.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace ra2;
using app::OptionsButton; using app::OptionsEvent; using app::OptionsPage;
namespace {
void check(bool ok,const char* what) { if(!ok) throw std::runtime_error(std::string("FAILED: ")+what); }
ui::Control button(std::uint32_t id,int x,int y,const char* label) { return {id,0x5000000b,0,128,{x,y,108,23},"",label}; }
// Copies of the shipped templates (game.exe resources 181 and 182).
ui::Dialog options_dialog() {
    ui::Dialog d{181,{0,0,426,295},"MS Sans Serif",8,{}};
    d.controls={button(1670,318,257,"Resume Mission"),button(1313,318,122,"Game Controls"),
        button(1310,318,149,"Load Game"),button(1314,318,230,"Abort Mission"),
        button(1311,318,176,"Save Game"),{1684,0x50020001,0,130,{318,1,108,10},"","Game Options"},
        {1685,0x50000200,0,130,{10,282,303,12},"",""},button(1312,317,203,"Delete Game")};
    return d;
}
ui::Dialog confirm_dialog() {
    ui::Dialog d{182,{0,0,426,295},"MS Sans Serif",8,{}};
    d.controls={button(1670,318,257,"Resume Mission"),
        {65535,0x50000201,0,130,{44,137,230,19},"","What would you like to do?"},
        button(1737,318,122,""),button(1810,318,149,""),{1685,0x50000200,0,130,{10,282,303,12},"",""}};
    return d;
}
const app::OptionsButtonView* find(const std::vector<app::OptionsButtonView>& v,OptionsButton id) {
    auto it=std::find_if(v.begin(),v.end(),[&](const auto& b){return b.id==id;});
    return it==v.end()?nullptr:&*it;
}
void click(app::OptionsMenu& m,const app::OptionsButtonView& b,OptionsEvent want,const char* what) {
    m.pointer_down(b.bounds.x+5,b.bounds.y+5);
    check(m.pointer_up(b.bounds.x+5,b.bounds.y+5)==want,what);
}
void state_checks() {
    app::OptionsMenu m(options_dialog(),confirm_dialog(),"Quit");
    check(!m.visible() && m.buttons().empty(),"menu starts closed");
    check(m.activate(OptionsButton::ResumeMission)==OptionsEvent::None,"closed menu ignores buttons");
    m.show();
    auto v=m.buttons();
    check(v.size()==6,"six original buttons");
    const OptionsButton order[]={OptionsButton::GameControls,OptionsButton::LoadGame,OptionsButton::SaveGame,
                                 OptionsButton::DeleteGame,OptionsButton::AbortMission};
    for(int i=0;i<5;++i) {
        const auto* b=find(v,order[i]); check(b,"button present");
        check(b->bounds.x==876 && b->bounds.y==227+25*i && b->bounds.width==125 && b->bounds.height==25,"original row");
    }
    check(find(v,OptionsButton::GameControls)->label=="Game Controls","label from dialog");
    const auto* resume=find(v,OptionsButton::ResumeMission);
    check(resume->bounds.y==702 && resume->label=="Resume Mission","resume at the bottom");
    check(m.heading()=="Game Options","heading from dialog");
    // Not wired: drawn disabled, and clicks do nothing.
    check(!find(v,OptionsButton::SaveGame)->enabled,"save starts disabled");
    click(m,*find(v,OptionsButton::SaveGame),OptionsEvent::None,"disabled save ignores click");
    check(m.activate(OptionsButton::LoadGame)==OptionsEvent::None,"disabled load ignores key");
    m.set_enabled(OptionsButton::SaveGame,true);
    click(m,*find(m.buttons(),OptionsButton::SaveGame),OptionsEvent::SaveGame,"enabled save reports");
    check(m.page()==OptionsPage::Options,"save keeps the menu open");
    m.set_enabled(OptionsButton::GameControls,true);m.set_enabled(OptionsButton::LoadGame,true);
    m.set_enabled(OptionsButton::DeleteGame,true);
    check(m.activate(OptionsButton::GameControls)==OptionsEvent::GameControls,"controls event");
    check(m.activate(OptionsButton::LoadGame)==OptionsEvent::LoadGame,"load event");
    check(m.activate(OptionsButton::DeleteGame)==OptionsEvent::DeleteGame,"delete event");
    // Press on one button and release on another runs nothing.
    v=m.buttons();
    const auto* a=find(v,OptionsButton::AbortMission);
    m.pointer_down(a->bounds.x+2,a->bounds.y+2);
    check(m.pointer_up(resume->bounds.x+2,resume->bounds.y+2)==OptionsEvent::None,"drag off cancels");
    check(m.page()==OptionsPage::Options,"drag off keeps page");
    m.pointer_move(a->bounds.x+2,a->bounds.y+2);
    check(find(m.buttons(),OptionsButton::AbortMission)->hovered,"hover state");
    check(m.pointer_up(a->bounds.x+2,a->bounds.y+2)==OptionsEvent::None,"release without press");
    // Abort asks first.
    click(m,*a,OptionsEvent::None,"abort opens the question");
    check(m.page()==OptionsPage::ConfirmAbort,"abort question shown");
    check(m.prompt()=="What would you like to do?","prompt from dialog");
    v=m.buttons();
    check(v.size()==2,"question has Quit and Resume");
    const auto* leave=find(v,OptionsButton::Leave);
    check(leave && leave->label=="Quit" && leave->bounds.y==227,"quit in the first row");
    check(m.activate(OptionsButton::SaveGame)==OptionsEvent::None,"question hides options buttons");
    check(m.escape()==OptionsEvent::None && m.page()==OptionsPage::Options,"Esc returns to options");
    check(m.escape()==OptionsEvent::Resume && !m.visible(),"Esc resumes play");
    check(m.escape()==OptionsEvent::None,"Esc on closed menu does nothing");
    m.show(); m.activate(OptionsButton::AbortMission);
    click(m,*find(m.buttons(),OptionsButton::ResumeMission),OptionsEvent::Resume,"question resume");
    check(!m.visible(),"question resume closes the menu");
    m.show(); m.activate(OptionsButton::AbortMission);
    check(m.activate(OptionsButton::Leave)==OptionsEvent::Quit && !m.visible(),"quit ends the match");
    m.show();
    click(m,*find(m.buttons(),OptionsButton::ResumeMission),OptionsEvent::Resume,"resume button");
    // Drawing a closed menu or a wrong frame size changes nothing.
    app::OptionsMenuArt none;
    render::Bitmap small{10,10,render::Bytes(400,7)};
    m.show(); app::draw_options_menu(small,m,none);
    check(small.rgba==render::Bytes(400,7),"wrong size is ignored");
    render::Bitmap bad{1024,768,render::Bytes(3,7)};
    app::draw_options_menu(bad,m,none);
    check(bad.rgba==render::Bytes(3,7),"short buffer is ignored");
}
void original_checks(const std::string& dir) {
    std::ifstream in(dir+"/game.exe",std::ios::binary);
    if(!in) { std::cout<<"skip original checks: no game.exe in "<<dir<<"\n"; return; }
    const std::vector<std::uint8_t> exe((std::istreambuf_iterator<char>(in)),{});
    fs::MixFileSystem files(dir); files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2); files.mount(app::side_archive(false));
    auto csf=files.read_file("RA2.CSF"); check(csf.has_value(),"RA2.CSF");
    ra2text::CsfFile strings; strings.parse(csf->data(),csf->size());
    auto m=app::OptionsMenu::load(exe,strings);
    m.show();
    auto v=m.buttons();
    check(v.size()==6,"original dialog 181 buttons");
    check(find(v,OptionsButton::DeleteGame)->bounds.y==302,"original delete row");
    check(find(v,OptionsButton::AbortMission)->label=="Abort Mission","original abort label");
    m.activate(OptionsButton::AbortMission);
    check(find(m.buttons(),OptionsButton::Leave)->label=="Quit","original GUI:Leave label");
    check(m.prompt()=="What would you like to do?","original GUI:AskAbortMission");
    m.escape();
    const auto art=app::OptionsMenuArt::load(files);
    render::Bitmap f{1024,768,render::Bytes(1024*768*4,0)};
    app::draw_options_menu(f,m,art);
    auto px=[&](int x,int y){return &f.rgba[(std::size_t(y)*1024+x)*4];};
    check(px(400,300)[2]>px(400,300)[0],"blue Allied background");
    // Game Controls is disabled until wired, so its caption is dark blue.
    bool dark=false,yellow=false;
    for(int y=227;y<252;++y)for(int x=876;x<1001;++x){auto p=px(x,y);
        dark|=p[0]==0&&p[1]==81&&p[2]==115;}
    for(int y=327;y<352;++y)for(int x=876;x<1001;++x){auto p=px(x,y);
        yellow|=p[0]==255&&p[1]==255&&p[2]==0;}
    check(dark,"disabled caption color");
    check(yellow,"enabled abort caption color");
    if(const char* out=std::getenv("RA2_OPTIONS_PPM")) {
        std::ofstream o(out,std::ios::binary); o<<"P6\n1024 768\n255\n";
        for(std::size_t i=0;i<f.rgba.size();i+=4) o.write(reinterpret_cast<const char*>(&f.rgba[i]),3);
    }
}
}
int main(int argc,char** argv) {
    try {
        state_checks();
        if(argc>1) original_checks(argv[1]);
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
    std::cout<<"battle options ok\n";
}
