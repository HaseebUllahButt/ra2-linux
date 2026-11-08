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
enum class Action { None, PageChanged, SetupChanged, Quit };
enum class Key { Escape };
enum class Opponent { Closed = 1, Hard = 4, Medium = 5, Easy = 6 };
struct SkirmishOpponentChoice {
    Opponent value;
    std::string name, name_key;
};
std::vector<SkirmishOpponentChoice> load_skirmish_opponents(const ra2text::CsfFile& strings);
struct OpponentSlot {
    std::optional<Opponent> opponent;
    int country = -2; // Original default sentinel.
    int color = -2;
};
struct SkirmishSetup {
    // scenario_index is the preserved saved value, not the selected catalog index.
    std::optional<int> game_mode, scenario_index, game_speed, credits, unit_count;
    int min_credits=2500, max_credits=10000, credit_increment=100;
    std::optional<bool> short_game, super_weapons, build_off_ally, mcv_repacks, crates;
    std::optional<std::string> country, map_filename, map_archive;
    std::optional<int> color;
    std::array<OpponentSlot,7> opponents;
};
struct SkirmishMap {
    std::string id, filename, name, description_key, source_archive;
    int min_players = 2, max_players = 4;
    std::vector<std::string> modes;
    bool available = false;
    bool built_in = false;
};
struct SkirmishMode {
    int id;
    std::string name, name_key, rules_file, map_filter;
};
std::vector<SkirmishMode> load_skirmish_modes(const fs::MixFileSystem& files,
                                               const ra2text::CsfFile& strings);
// Tests the original map mode filter, including the default standard mode.
bool skirmish_map_matches_mode(const SkirmishMap& map, const SkirmishMode& mode);
// Returns nullopt until the original saved-index ordering is recovered.
std::optional<SkirmishMap> resolve_saved_skirmish_map(const SkirmishSetup& setup,
    const std::vector<SkirmishMap>& maps, const std::vector<SkirmishMode>& modes);
struct SkirmishCountry {
    int index;
    std::string id, name, name_key;
};
struct SkirmishColor {
    int index;
    std::optional<gfx::Rgba> swatch;
    std::string name, name_key;
};
std::vector<SkirmishColor> load_skirmish_colors(const ra2text::CsfFile& strings);
// Original country order and random option, from the edition's rules.
std::vector<SkirmishCountry> load_skirmish_countries(const fs::MixFileSystem& files,
                                                       const ra2text::CsfFile& strings);
// Built-in packet order, followed by local PKT and MMX/YRO entries. Local
// archive order is sorted; Windows enumeration order is not specified.
std::vector<SkirmishMap> load_skirmish_maps(const fs::MixFileSystem& files,
                                             const ra2text::CsfFile& strings);
// Original dialog 258. 0x006AE6E0 and 0x004E37D0 map player slots.
struct SkirmishControls {
    static constexpr std::uint32_t choose_map=1450, start=1559, credits=1297;
    static constexpr std::uint32_t player_country=1697, player_color=1698;
    static constexpr std::array<std::uint32_t,7> opponents{1291,1294,1302,1306,1307,1308,1309};
    static constexpr std::array<std::uint32_t,7> countries{1296,1299,1310,1300,1311,1312,1313};
    static constexpr std::array<std::uint32_t,7> colors{1314,1315,1316,1317,1318,1319,1320};
};
// Apply built-in defaults, optional base and expansion rules, then preferences.
SkirmishSetup read_skirmish_setup(const rules::IniFile& preferences,
                                  const rules::IniFile* base_rules = nullptr,
                                  const rules::IniFile* expansion_rules = nullptr);
// Original horizontal credits slider mapping. Invalid ranges return nullopt.
std::optional<int> credit_from_slider_pointer(int local_x, int control_width,
                                              int minimum, int maximum, int increment) noexcept;
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
// Original map picker template, resource 107.
Dialog load_map_selection_dialog(const std::vector<std::uint8_t>& exe, const ra2text::CsfFile& strings);
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
// Item heights used by the original combo box. The outer window border is
// separate and is not recovered yet.
struct ComboItemMetrics { std::uint32_t selection_height, list_height; };
ComboItemMetrics combo_item_metrics(const GameFont& font);
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
    bool choosing_map() const noexcept { return choosing_map_; }
    const SkirmishSetup& skirmish_setup() const noexcept { return setup_; }
    const SkirmishSetup& setup() const noexcept { return setup_; }
    gfx::RgbaImage render() const;
    void pointer_move(int x, int y);
    void pointer_down(int x, int y);
    // Positive delta scrolls up. Coordinates are in the 800x600 client.
    void pointer_scroll(int x, int y, int delta);
    Action pointer_up(int x, int y);
    Action key_down(Key key) noexcept;
    // Configure catalogs from the prepared filesystem before selecting a map.
    void configure_maps(std::vector<SkirmishMap> maps, std::vector<SkirmishMode> modes);
    Action select_mode(int id);
    Action select_map(std::size_t index);
    // Slot zero is the player; slots one through seven are opponents.
    Action select_opponent(std::size_t slot, Opponent value) noexcept;
    Action select_country(std::size_t slot, int country_index);
    Action select_color(std::size_t slot, int color_index) noexcept;
private:
    struct DropChoice { int value; std::string label; std::optional<gfx::Rgba> swatch; };
    struct Dropdown { std::uint32_t id; std::size_t slot; int kind; Rect bounds; bool enabled;
                      std::vector<DropChoice> choices; int current; };
    std::vector<Dropdown> dropdowns() const;
    Rect dropdown_list_bounds(const Dropdown& dropdown) const;
    struct Button { std::uint32_t id; Rect bounds; std::string label; bool enabled; };
    std::vector<Button> buttons() const;
    std::optional<Rect> map_list_bounds(std::uint32_t id) const;
    std::vector<std::size_t> visible_map_indices() const;
    std::vector<Button> checks() const;
    std::optional<Rect> credit_bounds() const;
    void move_credit(int x);
    gfx::RgbaImage background_, map_background_, normal_, hover_, pressed_;
    gfx::RgbaImage popup_, popup_normal_, popup_hover_, popup_pressed_;
    gfx::RgbaImage check_on_, check_off_, arrow_normal_, arrow_pressed_, arrow_up_;
    gfx::RgbaImage slider_grip_, slider_left_, slider_middle_, slider_right_;
    std::string exit_prompt_, exit_ok_, exit_cancel_;
    bool confirming_exit_ = false;
    bool choosing_map_ = false;
    bool held_map_scroll_ = false;
    std::optional<int> pending_mode_;
    std::optional<std::size_t> pending_map_;
    std::size_t mode_scroll_=0, map_scroll_=0;
    std::optional<std::uint32_t> held_map_list_;
    std::optional<std::size_t> held_map_row_;
    bool credit_changed_ = false;
    SkirmishSetup setup_;
    std::vector<SkirmishCountry> countries_;
    std::vector<SkirmishColor> colors_;
    std::vector<SkirmishOpponentChoice> opponent_choices_;
    std::vector<SkirmishMap> maps_;
    std::vector<SkirmishMode> modes_;
    GameFont font_;
    Dialog main_, single_, skirmish_, map_dialog_;
    MenuNavigation navigation_;
    std::optional<std::uint32_t> hovered_, held_, open_dropdown_, held_dropdown_;
    std::optional<int> held_dropdown_item_;
};
}
