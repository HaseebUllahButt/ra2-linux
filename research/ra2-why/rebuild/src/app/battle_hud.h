#pragma once
// In-game battle HUD in the original base RA2 1024x768 layout:
// - a 168 pixel sidebar at x 856: credits, two top buttons, radar with the
//   eagle logo, repair and sell, four tabs, a 2x10 cameo grid, power bar;
// - a 32 pixel command bar under the battlefield (y 736..767).
// The art is the installed original art (see hud_art.h). Without it the
// HUD draws plain shapes in the same places, so clicks work the same.
// Draw once per frame, then run handle_click for each HUD click.
// The root validates every action and moves the camera itself.

#include "hud_art.h"
#include "../gfx/palette.h"
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

namespace ra2::fs { class MixFileSystem; }
namespace ra2::rules { class IniFile; }

namespace ra2::app {

// Defenses: buildings with BuildCat=Combat. load_assets also finds them
// from rules, so the root may keep sending Buildings.
enum class BuildCategory { All, Buildings, Infantry, Vehicles, Aircraft, Defenses };

struct BuildChoice {
    std::string type;     // Rules type name, the Build action payload.
    std::string name;     // Display name for the tooltip.
    int cost = 0;         // Credits shown in the tooltip.
    bool enabled = false; // False dims the entry and rejects clicks.
    int progress = 0;     // 0..1000 production clock, 1000 means ready.
    BuildCategory category = BuildCategory::All;
    int queued = 0;       // Queue length including the active item. 2+ shows a count.
    bool held = false;    // Manual production hold. Left click resumes.
};

// Everything the HUD needs for one draw pass.
enum class BuildingTool { None, Repair, Sell };

struct HudState {
    int cash = 0;
    int power_output = 0; // Power produced.
    int power_drain = 0;  // Power consumed.
    std::vector<std::string> selected_labels; // Kept for the root; the original sidebar shows none.
    std::vector<BuildChoice> choices;         // Build palette entries.
    render::Bitmap minimap;                   // Radar image. Empty means radar off (eagle logo).
    render::Rectangle camera;                 // Viewport rect in map pixels.
    int map_width = 0;                        // Full map size in map pixels.
    int map_height = 0;
    std::string status;                       // Message line, top left of the battlefield.
    std::string result;                       // Empty, or VICTORY / DEFEAT / DRAW end banner.
    int pointer_x = -1;                       // Mouse position, view pixels. -1 hides hover and tooltip.
    int pointer_y = -1;
    BuildingTool building_tool = BuildingTool::None;
    bool can_deploy = false;                  // Enables the Deploy button of the command bar.
};

enum class HudActionKind { None, Build, Deploy, Minimap, Options, Repair, Sell };

struct HudAction {
    HudActionKind kind = HudActionKind::None;
    std::string type;       // Build: rules type name.
    render::Point position; // Minimap: viewport center, in map pixels.
};

class BattleHud {
public:
    // Fixed layout, in view pixels. Matched against original screenshots.
    static constexpr int view_width = 1024;
    static constexpr int view_height = 768;
    static constexpr int sidebar_width = 168;
    static constexpr int sidebar_x = view_width - sidebar_width; // 856.
    static constexpr int command_bar_y = 736;                     // Battlefield is 856x736.
    static constexpr int command_bar_height = view_height - command_bar_y;
    static constexpr int build_columns = 2;
    static constexpr int build_rows = 10;
    static constexpr int build_button_width = 60;   // Cameo size.
    static constexpr int build_button_height = 48;
    static constexpr int build_button_pitch_x = 63;
    static constexpr int build_button_pitch_y = 50; // SIDE2.SHP strip height.
    static constexpr int palette_x = 878;           // First cameo origin.
    static constexpr int palette_y = 228;
    static constexpr int strip_y = 227;             // First SIDE2.SHP strip.
    static constexpr int tab_y = 197;
    static constexpr int command_slot_x = 48;       // First command bar button.
    static constexpr int command_slot_width = 52;
    static constexpr int command_slots = 15;
    // [AdvancedCommandBar] ButtonList in UI.INI: Team01, Team02, TypeSelect,
    // Deploy, Guard, PlanningMode. Deploy is the fourth button.
    static constexpr int deploy_slot = 3;
    static constexpr int deploy_width = command_slot_width;
    static constexpr int deploy_height = command_bar_height;
    static constexpr int deploy_y = command_bar_y;
    static constexpr int minimap_x = 868;           // Radar screen inside RADAR.SHP.
    static constexpr int minimap_y = 48;
    static constexpr int minimap_region_width = 142;
    static constexpr int minimap_region_height = 110;

    // Load the sidebar art of one side, the cameos and the font.
    // Files must outlive this HUD. Missing art uses the fallback shapes.
    void load_assets(const fs::MixFileSystem& files, const rules::IniFile& rules,
                     const rules::IniFile& art, const std::vector<BuildChoice>& choices,
                     bool soviet = false);
    bool original_art() const noexcept { return art_.ready(); }

    // Draw the sidebar and the command bar over the frame. Clips against the frame.
    void draw(render::Bitmap& frame, const HudState& state) const;

    // Map a HUD click to an action. Needs the same state as draw.
    // Tab and scroll clicks change the HUD and return None.
    HudAction handle_click(int x, int y, const HudState& state) const;

    // Fixed regions, in view pixels.
    static render::Rectangle build_button_bounds(int index); // Visible slot 0..19.
    static render::Rectangle minimap_bounds();
    static render::Rectangle deploy_bounds();
    render::Rectangle options_bounds() const;
    render::Rectangle repair_bounds() const;
    render::Rectangle sell_bounds() const;
    static render::Rectangle scroll_up_bounds();   // R-UP.SHP.
    static render::Rectangle scroll_down_bounds(); // R-DN.SHP.
    render::Rectangle tab_bounds(int tab) const;   // Tab 0..3: buildings, defenses, infantry, vehicles.
    // Tab of one choice, and the slot rect of a choice when its tab is active
    // and it is scrolled into view. Empty rect otherwise.
    int tab_of(const BuildChoice& choice) const;
    int active_tab() const noexcept { return active_tab_; }
    render::Rectangle choice_bounds(const HudState& state, const std::string& type) const;
private:
    mutable int active_tab_ = 0;
    mutable int top_row_ = 0;
    mutable int radar_frame_ = 0;  // 0 eagle .. 32 open radar.
    mutable bool panel_valid_ = false;
    mutable int panel_tab_ = -1, panel_row_ = -1;
    mutable HudState panel_state_;
    mutable render::Bitmap panel_;
    HudArt art_;
    std::set<std::string> defenses_;
    bool same_panel(const HudState& state) const;
    void settle(const HudState& state) const;
    std::vector<const BuildChoice*> visible(const HudState& state) const;
    void draw_panel(render::Bitmap& frame, const HudState& state) const;
    void draw_fallback_panel(render::Bitmap& frame, const HudState& state) const;
    const fs::MixFileSystem* cameo_files_ = nullptr;
    std::optional<gfx::Palette> cameo_palette_;
    std::unordered_map<std::string, std::string> cameo_names_;
    mutable std::unordered_map<std::string, render::Bitmap> cameos_;
    const render::Bitmap* cameo(const std::string& type) const;
};
} // namespace ra2::app
