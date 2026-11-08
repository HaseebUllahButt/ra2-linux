#pragma once
#include "../gfx/shp_decode.h"
#include "../text/csf.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::fs { class MixFileSystem; }
namespace ra2::rules { class IniFile; }
namespace ra2::ui {
// Original dialog resource units. Convert using the actual UI font metrics.
struct Rect { int x, y, width, height; };
// Windows dialog units use average character width and font height.
// The caller supplies the original Windows font metrics, if known.
Rect dialog_to_pixels(Rect bounds, unsigned base_width, unsigned base_height);
struct Control {
    std::uint32_t id, style, extended_style;
    std::uint16_t class_id;
    Rect bounds;
    std::string label_key, label;
};
enum class Page { Main, SinglePlayer, Skirmish };
// Quit is returned after confirmation.
enum class Action { None, PageChanged, Quit };
enum class Key { Escape };
enum class Opponent { Closed = 1, Easy = 4, Medium = 5, Hard = 6 };
struct OpponentSlot {
    std::optional<Opponent> opponent;
    int country = -2; // Original default sentinel.
    int color = -2;
};
struct SkirmishSetup {
    std::optional<int> game_mode, scenario_index, game_speed, credits, unit_count;
    std::optional<bool> short_game, super_weapons, build_off_ally, mcv_repacks, crates;
    std::optional<std::string> country;
    std::optional<int> color;
    std::array<OpponentSlot,7> opponents;
};
// Original dialog 258. 0x006AE6E0 and 0x004E37D0 map player slots.
struct SkirmishControls {
    static constexpr std::uint32_t choose_map=1450, start=1559, credits=1297;
    static constexpr std::uint32_t player_country=1697, player_color=1698;
    static constexpr std::array<std::uint32_t,7> opponents{1291,1294,1302,1306,1307,1308,1309};
    static constexpr std::array<std::uint32_t,7> countries{1296,1299,1310,1300,1311,1312,1313};
    static constexpr std::array<std::uint32_t,7> colors{1314,1315,1316,1317,1318,1319,1320};
};
// Read the original saved preferences. Missing rule-derived defaults are unset.
SkirmishSetup read_skirmish_setup(const rules::IniFile& preferences);
// Activates only the supported page changes. IDs are from the original dialog.
class MenuNavigation {
public:
    Page page() const noexcept { return page_; }
    Action activate(std::uint32_t control_id) noexcept;
private:
    Page page_ = Page::Main;
};
struct Dialog {
    std::uint16_t resource_id;
    Rect bounds;
    std::string font;
    std::uint16_t font_points;
    std::vector<Control> controls;
    // Input is in original dialog units. Only owner-drawn buttons have
    // verified hit bounds. Returns only IDs explicitly enabled.
    std::optional<std::uint32_t> hit_test(int x, int y,
                                          const std::vector<std::uint32_t>& enabled) const;
    std::optional<std::uint32_t> hit_test_pixels(int x, int y, unsigned base_width,
                                                 unsigned base_height,
                                                 const std::vector<std::uint32_t>& enabled) const;
};
// Reads the original 32-bit PE dialog templates; no Windows dependency.
Dialog load_dialog(const std::vector<std::uint8_t>& exe, Page page, const ra2text::CsfFile& strings);
class GameFont {
public:
    static GameFont parse(std::vector<std::uint8_t> bytes);
    // A missing code point returns nullopt. Pixels are transparent or color.
    std::optional<gfx::RgbaImage> glyph(char16_t codepoint, gfx::Rgba color) const;
    // Draws UTF-8 text in the original bitmap font, clipped to the image.
    void draw_text(gfx::RgbaImage& image, std::string_view utf8, int x, int y, gfx::Rgba color) const;
    std::uint32_t text_width(std::string_view utf8) const;
    std::uint32_t line_height() const noexcept { return line_height_; }
private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t max_width_=0, row_bytes_=0, height_=0, line_height_=0;
    std::uint32_t count_=0, glyph_bytes_=0;
};
// Decodes the original 8-bit, single-plane PCX variant to opaque RGBA.
gfx::RgbaImage decode_pcx(const std::vector<std::uint8_t>& bytes);
// Static shell background at the original 800x600 resolution. Mount NTRLMD.MIX
// and NEUTRAL.MIX before calling. Other resolutions are not yet implemented.
gfx::RgbaImage draw_shell_background(const fs::MixFileSystem& files, unsigned width, unsigned height);
class Menu {
public:
    static Menu load(const fs::MixFileSystem& files, const std::vector<std::uint8_t>& exe,
                     const ra2text::CsfFile& strings);
    Page page() const noexcept { return navigation_.page(); }
    bool confirming_exit() const noexcept { return confirming_exit_; }
    const SkirmishSetup& skirmish_setup() const noexcept { return setup_; }
    gfx::RgbaImage render() const;
    void pointer_move(int x, int y);
    void pointer_down(int x, int y);
    Action pointer_up(int x, int y);
    Action key_down(Key key) noexcept;
private:
    struct Button { std::uint32_t id; Rect bounds; std::string label; bool enabled; };
    std::vector<Button> buttons() const;
    gfx::RgbaImage background_, normal_, hover_, pressed_;
    gfx::RgbaImage popup_, popup_normal_, popup_hover_, popup_pressed_;
    std::string exit_prompt_, exit_ok_, exit_cancel_;
    bool confirming_exit_ = false;
    SkirmishSetup setup_;
    GameFont font_;
    Dialog main_, single_, skirmish_;
    MenuNavigation navigation_;
    std::optional<std::uint32_t> hovered_, held_;
};
}
