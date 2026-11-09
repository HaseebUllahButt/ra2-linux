#pragma once
// In-game battle HUD: fixed 220 pixel right sidebar in a 1024x768 view.
// Draw once per frame, then run handle_click for each sidebar click.
// The root validates every action and moves the camera itself.
// A non-empty result draws a VICTORY / DEFEAT / DRAW banner over the
// battlefield. The status line shows in the sidebar.
// Fallback TODO-ORIGINAL art: minimal embedded bitmap font and plain
// rectangles stand in for the original sidebar shapes.

#include "../render/minimap.h"
#include "../gfx/palette.h"
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace ra2::fs { class MixFileSystem; }
namespace ra2::rules { class IniFile; }

// The sidebar API needs a plain 2-D point. Render had no point type, so the
// HUD owns this one.
namespace ra2::render {
struct Point { int x = 0, y = 0; };
} // namespace ra2::render

namespace ra2::app {

// One purchasable entry in the build palette.
enum class BuildCategory { All, Buildings, Infantry, Vehicles, Aircraft };

struct BuildChoice {
    std::string type;     // Rules type name, the Build action payload.
    std::string name;     // Short display label.
    int cost = 0;         // Credits shown on the button.
    bool enabled = false; // False dims the entry and rejects clicks.
    int progress = 0;     // 0..1000 production clock, 1000 means ready.
    BuildCategory category = BuildCategory::All;
    int queued = 0;       // Items waiting after the current one. A count of 2+ shows on the cameo.
};

// Everything the sidebar needs for one draw pass.
struct HudState {
    int cash = 0;
    int power_output = 0; // Power produced.
    int power_drain = 0;  // Power consumed.
    std::vector<std::string> selected_labels; // Names of selected units.
    std::vector<BuildChoice> choices;         // Build palette entries.
    render::Bitmap minimap;                   // Optional radar image.
    render::Rectangle camera;                 // Viewport rect in map pixels.
    int map_width = 0;                        // Full map size in map pixels.
    int map_height = 0;
    std::string status;                       // Sidebar status line, e.g. "BUILDING: GVGIANT".
    std::string result;                       // Empty, or VICTORY / DEFEAT / DRAW end banner.
    int pointer_x = -1;                       // Mouse position, view pixels. -1 hides the tooltip.
    int pointer_y = -1;
    bool can_deploy = false;                   // Root sets this from selected live MCVs. DEPLOY shows only when true.
};

// Click results. The root executes them and moves the camera.
enum class HudActionKind { None, Build, Deploy, Minimap };

struct HudAction {
    HudActionKind kind = HudActionKind::None;
    std::string type;       // Build: rules type name.
    render::Point position; // Minimap: viewport center, in map pixels.
};

class BattleHud {
public:
    // Fixed sidebar layout, in view pixels.
    static constexpr int view_width = 1024;
    static constexpr int view_height = 768;
    static constexpr int sidebar_width = 220;
    static constexpr int sidebar_x = view_width - sidebar_width; // 804.
    static constexpr int build_columns = 2;
    static constexpr int build_rows = 8;
    static constexpr int build_button_width = 104;
    static constexpr int build_button_height = 56;
    static constexpr int build_button_pitch_x = 108; // 104 button + 4 gap.
    static constexpr int build_button_pitch_y = 62;  // 56 button + 6 gap.
    static constexpr int palette_x = 808;            // First button origin.
    static constexpr int palette_y = 60;
    static constexpr int deploy_width = 212;
    static constexpr int deploy_height = 40;
    static constexpr int deploy_y = 560;
    static constexpr int minimap_x = sidebar_x; // Region, see minimap_bounds.
    static constexpr int minimap_y = 672;
    static constexpr int minimap_region_width = sidebar_width;
    static constexpr int minimap_region_height = 96;

    // Register cameo images. Decode them on first draw. Files must outlive this HUD.
    // Missing images use the fallback art.
    void load_assets(const fs::MixFileSystem& files, const rules::IniFile& rules,
                     const rules::IniFile& art, const std::vector<BuildChoice>& choices);

    // Draw the whole sidebar over the frame. Clips against the frame.
    void draw(render::Bitmap& frame, const HudState& state) const;

    // Map a sidebar click to an action. Needs the same state as draw.
    HudAction handle_click(int x, int y, const HudState& state) const;

    // Fixed regions, in view pixels.
    static render::Rectangle build_button_bounds(int index);
    static render::Rectangle minimap_bounds();
    static render::Rectangle deploy_bounds();
private:
    mutable BuildCategory active_category_ = BuildCategory::All;
    mutable bool panel_valid_=false;
    mutable BuildCategory panel_category_=BuildCategory::All;
    mutable HudState panel_state_;
    mutable render::Bitmap panel_;
    bool same_panel(const HudState& state) const;
    void draw_panel(render::Bitmap& frame,const HudState& state) const;
    const fs::MixFileSystem* cameo_files_=nullptr;
    std::optional<gfx::Palette> cameo_palette_;
    std::unordered_map<std::string,std::string> cameo_names_;
    mutable std::unordered_map<std::string, render::Bitmap> cameos_;
    const render::Bitmap* cameo(const std::string& type) const;
};
} // namespace ra2::app
