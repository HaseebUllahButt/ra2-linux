#pragma once
// Original base RA2 sidebar art for the battle HUD.
// The art comes from SIDEC01.MIX (Allies) or SIDEC02.MIX (Soviets) with
// SIDEBAR.PAL, the end banner from GRFXTXT.SHP with GRFXTXT.PAL, and text
// from GAME.FNT. The side MIX is read in place; the file system is not changed.
// Positions below were matched pixel by pixel against original 1024x768
// screenshots (reference/shots and qa/reports/qa5-side-by-side).

#include "../render/minimap.h"
#include "../ui/ui.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::fs { class MixFileSystem; }

// The sidebar API needs a plain 2-D point. Render had no point type, so the
// HUD owns this one.
namespace ra2::render {
struct Point { int x = 0, y = 0; };
} // namespace ra2::render

namespace ra2::app {

struct HudSprite {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // Row-major R,G,B,A. Alpha 0 is transparent.
};

// Per-side positions of the art that differs between SIDEC01 and SIDEC02.
struct HudSideLayout {
    render::Point diplomacy, options;   // DIPLOBTN.SHP and OPTBTN.SHP.
    render::Point repair, sell;         // REPAIR.SHP and SELL.SHP.
    int tab_x = 0, tab_pitch = 0;       // TAB00..TAB03.SHP, y is fixed.
};
HudSideLayout hud_side_layout(bool soviet);

class HudArt {
public:
    // Loads all sidebar art. Returns false and stays empty when any part
    // is missing; the HUD then draws plain fallback shapes.
    bool load(const fs::MixFileSystem& files, bool soviet);
    bool ready() const noexcept { return ready_; }
    bool soviet() const noexcept { return soviet_; }
    // Lower-case SHP name, for example "side1.shp". Null when absent.
    const HudSprite* sprite(std::string_view shp, int frame = 0) const;
    int frames(std::string_view shp) const;
    const ui::GameFont* font() const noexcept { return font_.get(); }
private:
    bool ready_ = false, soviet_ = false;
    std::map<std::string, std::vector<HudSprite>, std::less<>> sprites_;
    std::shared_ptr<ui::GameFont> font_;
};

// Copies opaque sprite pixels. Clips against the frame and the clip rect.
void hud_blit(render::Bitmap& frame, const HudSprite& sprite, int x, int y,
              render::Rectangle clip = {0, 0, 1 << 30, 1 << 30});
// Mixes opaque sprite pixels 50/50 with the frame, like the original
// translucent build clock (GCLOCK2.SHP).
void hud_blend(render::Bitmap& frame, const HudSprite& sprite, int x, int y);
// Draws text in the original game font. Characters missing from the font
// are skipped. Returns the drawn width.
int hud_text(render::Bitmap& frame, const ui::GameFont& font, std::string_view text,
             int x, int y, gfx::Rgba color);
int hud_text_width(const ui::GameFont& font, std::string_view text);
} // namespace ra2::app
