#pragma once
// In-game options menu of base Red Alert 2 (Esc during a skirmish).
// Layout comes from the original game.exe dialogs 181 (options) and
// 182 (abort question). Art comes from the player's side archive,
// SIDEC01.MIX or SIDEC02.MIX (0x0051A330 formats "SIDEC%02d.MIX").
#include "../render/tmp_decode.h"
#include "../ui/ui.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ra2::fs { class MixFileSystem; }
namespace ra2::app {
// Original control IDs from dialogs 181 and 182.
enum class OptionsButton : std::uint32_t {
    GameControls=1313, LoadGame=1310, SaveGame=1311, DeleteGame=1312,
    AbortMission=1314, ResumeMission=1670, Leave=1737,
};
enum class OptionsPage { Closed, Options, ConfirmAbort };
// What the caller must do after an input. The menu changes its own page.
enum class OptionsEvent { None, Resume, Quit, GameControls, LoadGame, SaveGame, DeleteGame };
struct OptionsButtonView {
    OptionsButton id;
    ui::Rect bounds; // Frame pixels.
    std::string label;
    bool enabled=false, hovered=false, pressed=false;
};

// Menu state and input. It owns no art, so tests can use it without game files.
class OptionsMenu {
public:
    static constexpr int width=1024, height=768;
    OptionsMenu()=default;
    // Labels come from the dialogs. leave_label is CSF GUI:Leave.
    OptionsMenu(const ui::Dialog& options, const ui::Dialog& confirm, std::string leave_label);
    static OptionsMenu load(const std::vector<std::uint8_t>& exe, const ra2text::CsfFile& strings);

    OptionsPage page() const noexcept { return page_; }
    bool visible() const noexcept { return page_!=OptionsPage::Closed; }
    void show() noexcept;
    // Esc: the abort question goes back to the options; the options resume play.
    OptionsEvent escape() noexcept;
    // Runs a button as if it was clicked. Disabled buttons do nothing.
    OptionsEvent activate(OptionsButton id) noexcept;
    void pointer_move(int x,int y) noexcept;
    void pointer_down(int x,int y) noexcept;
    // A click runs a button when press and release are on the same button.
    OptionsEvent pointer_up(int x,int y) noexcept;
    // Caller decides which buttons have working actions.
    void set_enabled(OptionsButton id,bool enabled) noexcept;
    bool enabled(OptionsButton id) const noexcept;
    std::vector<OptionsButtonView> buttons() const;
    const std::string& prompt() const noexcept { return prompt_; }
    const std::string& heading() const noexcept { return heading_; }
private:
    struct Slot { OptionsButton id; int row; std::string label; };
    std::vector<Slot> options_, confirm_;
    std::string prompt_, heading_;
    std::array<bool,7> enabled_{false,false,false,false,true,true,true};
    OptionsPage page_=OptionsPage::Closed;
    std::optional<OptionsButton> hovered_, held_;
    const std::vector<Slot>& slots() const noexcept;
    std::optional<OptionsButton> hit(int x,int y) const noexcept;
};

// Original art for the menu. Mount the side archive before load.
struct OptionsMenuArt {
    gfx::RgbaImage background, credits, top, radar, side1, side2, side3, addon, spacer, left_cap, end_cap;
    std::array<gfx::RgbaImage,3> button; // Normal, hover, pressed.
    ui::GameFont font;
    static OptionsMenuArt load(const fs::MixFileSystem& files);
};
// Base 0x0051A330: Allied houses use SIDEC01.MIX, Soviet houses SIDEC02.MIX.
const char* side_archive(bool soviet) noexcept;
// Draws the full 1024x768 menu over frame. Other frame sizes are left unchanged.
void draw_options_menu(render::Bitmap& frame, const OptionsMenu& menu, const OptionsMenuArt& art);
}
