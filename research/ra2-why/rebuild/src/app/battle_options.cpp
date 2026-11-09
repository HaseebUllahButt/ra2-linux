// State and layout of the base in-game options menu (dialogs 181 and 182).
#include "battle_options.h"
#include <algorithm>

namespace ra2::app {
namespace {
constexpr int sidebar_x=BattleOptions::width-168;
// Matched against the original 1024x768 Esc screen: SIDEBTTN.SHP cells are
// 125x25, start 20 pixels into the sidebar and 227 pixels down, and step 25.
// The last button sits on SIDE3.SHP, which starts at 727.
constexpr int button_x=sidebar_x+20, first_row_y=227, row_step=25, back_y=702;
constexpr int button_w=125, button_h=25;
std::size_t index(OptionsButton id) noexcept {
    switch(id) {
    case OptionsButton::GameControls: return 0;
    case OptionsButton::LoadGame: return 1;
    case OptionsButton::SaveGame: return 2;
    case OptionsButton::DeleteGame: return 3;
    case OptionsButton::AbortMission: return 4;
    case OptionsButton::ResumeMission: return 5;
    case OptionsButton::Leave: return 6;
    }
    return 0;
}
bool known(std::uint32_t id) noexcept {
    return id==1313||id==1310||id==1311||id==1312||id==1314||id==1670||id==1737;
}
// Dialog rows are 27 dialog units apart from Y=122. Resume (1670) is the
// back button and always takes the bottom slot (row -1).
int row_of(const ui::Control& c) noexcept {
    if(c.id==1670) return -1;
    return std::max(0,(c.bounds.y-122+13)/27);
}
ui::Rect rect(int row) noexcept {
    return {button_x,row<0?back_y:first_row_y+row*row_step,button_w,button_h};
}
bool inside(ui::Rect r,int x,int y) noexcept {
    return x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height;
}
}

BattleOptions::BattleOptions(const ui::Dialog& options, const ui::Dialog& confirm, std::string leave_label) {
    for(const auto& c:options.controls) {
        if(c.class_id==128 && known(c.id)) options_.push_back({OptionsButton(c.id),row_of(c),c.label});
        if(c.id==1684) heading_=c.label;
    }
    for(const auto& c:confirm.controls) {
        if(c.id==1670) confirm_.push_back({OptionsButton::ResumeMission,-1,c.label});
        // 0x005E22B0 names 1737 "Leave" in the abort question. Its caption is
        // GUI:Blank in the template; the game sets the text at run time.
        if(c.id==1737) confirm_.push_back({OptionsButton::Leave,row_of(c),leave_label});
        // 1810 is Observe or Restart; neither applies to a skirmish.
        if(c.id==65535 && c.class_id==130) prompt_=c.label;
    }
}

void BattleOptions::show() noexcept { page_=OptionsPage::Options; hovered_.reset(); held_.reset(); }

OptionsEvent BattleOptions::escape() noexcept {
    held_.reset();
    if(page_==OptionsPage::ConfirmAbort) { page_=OptionsPage::Options; return OptionsEvent::None; }
    if(page_==OptionsPage::Options) { page_=OptionsPage::Closed; return OptionsEvent::Resume; }
    return OptionsEvent::None;
}

const std::vector<BattleOptions::Slot>& BattleOptions::slots() const noexcept {
    return page_==OptionsPage::ConfirmAbort?confirm_:options_;
}

OptionsEvent BattleOptions::activate(OptionsButton id) noexcept {
    if(page_==OptionsPage::Closed || !enabled(id)) return OptionsEvent::None;
    const auto& list=slots();
    if(std::none_of(list.begin(),list.end(),[&](const Slot& s){return s.id==id;})) return OptionsEvent::None;
    hovered_.reset(); held_.reset();
    switch(id) {
    case OptionsButton::AbortMission: page_=OptionsPage::ConfirmAbort; return OptionsEvent::None;
    case OptionsButton::ResumeMission: page_=OptionsPage::Closed; return OptionsEvent::Resume;
    case OptionsButton::Leave: page_=OptionsPage::Closed; return OptionsEvent::Quit;
    case OptionsButton::GameControls: return OptionsEvent::GameControls;
    case OptionsButton::LoadGame: return OptionsEvent::LoadGame;
    case OptionsButton::SaveGame: return OptionsEvent::SaveGame;
    case OptionsButton::DeleteGame: return OptionsEvent::DeleteGame;
    }
    return OptionsEvent::None;
}

std::optional<OptionsButton> BattleOptions::hit(int x,int y) const noexcept {
    if(page_==OptionsPage::Closed) return std::nullopt;
    for(const auto& s:slots()) if(enabled(s.id) && inside(rect(s.row),x,y)) return s.id;
    return std::nullopt;
}
void BattleOptions::pointer_move(int x,int y) noexcept { hovered_=hit(x,y); }
void BattleOptions::pointer_down(int x,int y) noexcept { hovered_=held_=hit(x,y); }
OptionsEvent BattleOptions::pointer_up(int x,int y) noexcept {
    const auto held=held_; held_.reset(); hovered_=hit(x,y);
    if(!held || held!=hovered_) return OptionsEvent::None;
    return activate(*held);
}
void BattleOptions::set_enabled(OptionsButton id,bool on) noexcept {
    enabled_[index(id)]=on;
    if(!on && hovered_==id) hovered_.reset();
    if(!on && held_==id) held_.reset();
}
bool BattleOptions::enabled(OptionsButton id) const noexcept { return enabled_[index(id)]; }

std::vector<OptionsButtonView> BattleOptions::buttons() const {
    std::vector<OptionsButtonView> out;
    if(page_==OptionsPage::Closed) return out;
    for(const auto& s:slots()) {
        const bool on=enabled(s.id);
        out.push_back({s.id,rect(s.row),s.label,on,on&&hovered_==s.id,on&&held_==s.id&&hovered_==s.id});
    }
    return out;
}
}
