// Reference: Phobos-YRpp/SidebarClass.h (cameo progress and strips),
// Phobos-YRpp/RadarClass.h (RadarRect), CnC_Red_Alert/CODE/RADAR.CPP
// (radar coordinate scaling), UI.INI [AdvancedCommandBar] (button order).
// Art positions were matched pixel by pixel against original screenshots.
#include "battle_hud.h"
#include "../loop/profile.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../gfx/shp_decode.h"
#include <algorithm>
#include <cctype>
#include <cstdint>

namespace ra2::app {
namespace {
using render::Rectangle;
using Color = std::array<unsigned char, 4>;
constexpr Color ink{224, 230, 192, 255}, dark{22, 29, 32, 255}, edge{104, 129, 126, 255};
// Original sidebar text blue, sampled from the credits counter.
constexpr gfx::Rgba side_text{165, 211, 255, 255};
constexpr int slots = BattleHud::build_columns * BattleHud::build_rows;
// Power bar: POWERP.SHP segments, 2 pixels high on a 3 pixel pitch, from
// y 725 upward at x 861. Frames: 0 unlit, 1 green, 2 yellow, 3 red.
constexpr int power_x = 861, power_bottom = 725, power_segments = 167;
// Command bar buttons in [AdvancedCommandBar] order.
constexpr const char* command_buttons[] = {"button00.shp", "button01.shp", "button03.shp",
                                           "button04.shp", "button06.shp", "button09.shp"};

bool valid(const render::Bitmap& b) {
    return b.width > 0 && b.height > 0 &&
        static_cast<std::uint64_t>(b.width) * b.height * 4 <= b.rgba.size();
}
void pixel(render::Bitmap& b, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= b.width || y >= b.height) return;
    auto i = (static_cast<std::size_t>(y) * b.width + x) * 4;
    std::copy(c.begin(), c.end(), b.rgba.begin() + i);
}
void fill(render::Bitmap& b, Rectangle r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(b.height, r.y + r.height); ++y)
        for (int x = std::max(0, r.x); x < std::min(b.width, r.x + r.width); ++x) pixel(b, x, y, c);
}
void border(render::Bitmap& b, Rectangle r, Color c) {
    fill(b, {r.x, r.y, r.width, 1}, c); fill(b, {r.x, r.y + r.height - 1, r.width, 1}, c);
    fill(b, {r.x, r.y, 1, r.height}, c); fill(b, {r.x + r.width - 1, r.y, 1, r.height}, c);
}
// Fallback only, when the original GAME.FNT is missing: a 3x5 font.
void tiny_text(render::Bitmap& b, int x, int y, const std::string& s, Color c, int limit, int scale = 1) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$:/-?.'";
    const char* glyphs[] = {"010101111101101","110101110101110","011100100100011",
        "110101101101110","111100110100111","111100110100100","011100101101011",
        "101101111101101","111010010010111","001001001101010","101101110101101",
        "100100100100111","101111111101101","101111111111101","010101101101010",
        "110101110100100","010101101111011","110101110101101","011100010001110",
        "111010010010010","101101101101111","101101101101010","101101111111101",
        "101101010101101","101101010010010","111001010100111","111101101101111",
        "010110010010111","110001010100111","110001010001110","101101111001001",
        "111100110001110","011100111101111","111001010010010","111101111101111",
        "111101111001110","011110010011110","000010000010000","001001010100100",
        "000000111000000","110001010000010","000000000000010","010010000000000"};
    const int end = x + limit;
    for (unsigned char raw : s) {
        if (x + 3 * scale > end) break;
        const auto n = chars.find(static_cast<char>(std::toupper(raw)));
        if (n != std::string::npos) for (int j = 0; j < 15; ++j)
            if (glyphs[n][j] == '1') fill(b, {x + (j % 3) * scale, y + (j / 3) * scale, scale, scale}, c);
        x += 4 * scale;
    }
}
bool inside(int x, int y, Rectangle r) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
void copy_rect(const render::Bitmap& from, render::Bitmap& to, Rectangle r) {
    const int x0 = std::max(0, r.x), x1 = std::min({to.width, from.width, r.x + r.width});
    for (int y = std::max(0, r.y); y < std::min({to.height, from.height, r.y + r.height}); ++y)
        if (x1 > x0) std::copy_n(from.rgba.begin() + (std::size_t(y) * from.width + x0) * 4,
            std::size_t(x1 - x0) * 4, to.rgba.begin() + (std::size_t(y) * to.width + x0) * 4);
}
// Font text with a one pixel black shadow, for messages over the battlefield.
void shadow_text(render::Bitmap& b, const ui::GameFont& font, const std::string& s, int x, int y, gfx::Rgba c) {
    hud_text(b, font, s, x + 1, y + 1, {0, 0, 0, 255});
    hud_text(b, font, s, x, y, c);
}
} // namespace

void BattleHud::load_assets(const fs::MixFileSystem& files, const rules::IniFile& rules,
                            const rules::IniFile& art, const std::vector<BuildChoice>& choices,
                            bool soviet) {
    panel_valid_ = false;
    art_.load(files, soviet);
    cameos_.clear(); cameo_names_.clear(); cameo_palette_.reset(); defenses_.clear();
    cameo_files_ = &files;
    for (const auto& c : choices)
        if (rules.read_string(c.type, "BuildCat", "") == "Combat") defenses_.insert(c.type);
    const auto pal = files.read_file("cameo.pal");
    if (!pal) return;
    try { cameo_palette_ = gfx::Palette::from_bytes(*pal); }
    catch (const std::exception&) { return; }
    for (const auto& c : choices) {
        const auto image = rules.read_string(c.type, "Image", c.type);
        cameo_names_[c.type] = art.read_string(image, "Cameo", image + "ICON") + ".shp";
    }
}
const render::Bitmap* BattleHud::cameo(const std::string& type) const {
    auto cached = cameos_.find(type);
    if (cached != cameos_.end()) return valid(cached->second) ? &cached->second : nullptr;
    if (!cameo_files_ || !cameo_palette_) return nullptr;
    const auto name = cameo_names_.find(type);
    if (name == cameo_names_.end()) return nullptr;
    render::Bitmap bitmap;
    const auto data = cameo_files_->read_file(name->second);
    if (data) try {
        auto decoded = gfx::ShpImage::parse(*data).decode_frame(0, *cameo_palette_);
        bitmap.width = decoded.width; bitmap.height = decoded.height; bitmap.rgba = std::move(decoded.pixels);
    } catch (const std::exception&) { /* Missing cameo: plain slot with the name. */ }
    const auto item = cameos_.emplace(type, std::move(bitmap)).first;
    return valid(item->second) ? &item->second : nullptr;
}

render::Rectangle BattleHud::build_button_bounds(int i) {
    if (i < 0 || i >= slots) return {};
    return {palette_x + i % build_columns * build_button_pitch_x,
            palette_y + i / build_columns * build_button_pitch_y, build_button_width, build_button_height};
}
render::Rectangle BattleHud::deploy_bounds() {
    return {command_slot_x + deploy_slot * command_slot_width, deploy_y, deploy_width, deploy_height};
}
render::Rectangle BattleHud::minimap_bounds() {
    return {minimap_x, minimap_y, minimap_region_width, minimap_region_height};
}
render::Rectangle BattleHud::scroll_down_bounds() { return {895, 734, 46, 25}; }
render::Rectangle BattleHud::scroll_up_bounds() { return {941, 734, 46, 25}; }
render::Rectangle BattleHud::tab_bounds(int tab) const {
    if (tab < 0 || tab > 3) return {};
    const auto layout = hud_side_layout(art_.soviet());
    const auto* s = art_.sprite("tab00.shp");
    return {layout.tab_x + tab * layout.tab_pitch, tab_y, s ? s->width : 28, s ? s->height : 27};
}
int BattleHud::tab_of(const BuildChoice& c) const {
    switch (c.category) {
    case BuildCategory::Defenses: return 1;
    case BuildCategory::Infantry: return 2;
    case BuildCategory::Vehicles: case BuildCategory::Aircraft: return 3;
    default: return defenses_.count(c.type) ? 1 : 0;
    }
}
// Keeps the active tab and the scroll row valid for this state. An empty
// active tab moves to the first tab with items.
void BattleHud::settle(const HudState& s) const {
    int count[4] = {};
    for (const auto& c : s.choices) ++count[tab_of(c)];
    if (!count[active_tab_])
        for (int t = 0; t < 4; ++t) if (count[t]) { active_tab_ = t; break; }
    const int rows = (count[active_tab_] + build_columns - 1) / build_columns;
    top_row_ = std::clamp(top_row_, 0, std::max(0, rows - build_rows));
}
std::vector<const BuildChoice*> BattleHud::visible(const HudState& s) const {
    std::vector<const BuildChoice*> out;
    int index = 0;
    for (const auto& c : s.choices) {
        if (tab_of(c) != active_tab_) continue;
        if (index++ < top_row_ * build_columns) continue;
        if (int(out.size()) == slots) break;
        out.push_back(&c);
    }
    return out;
}
render::Rectangle BattleHud::choice_bounds(const HudState& s, const std::string& type) const {
    settle(s);
    const auto list = visible(s);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i]->type == type) return build_button_bounds(int(i));
    return {};
}

render::Rectangle BattleHud::options_bounds() const {
    const auto at = hud_side_layout(art_.soviet()).options;
    const auto* sprite = art_.sprite("optbtn.shp");
    return {at.x, at.y, sprite ? sprite->width : 72, sprite ? sprite->height : 26};
}
render::Rectangle BattleHud::repair_bounds() const {
    const auto at = hud_side_layout(art_.soviet()).repair;
    const auto* sprite = art_.sprite("repair.shp");
    return {at.x, at.y, sprite ? sprite->width : 64, sprite ? sprite->height : 31};
}
render::Rectangle BattleHud::sell_bounds() const {
    const auto at = hud_side_layout(art_.soviet()).sell;
    const auto* sprite = art_.sprite("sell.shp");
    return {at.x, at.y, sprite ? sprite->width : 64, sprite ? sprite->height : 31};
}

HudAction BattleHud::handle_click(int x, int y, const HudState& s) const {
    settle(s);
    if (inside(x, y, options_bounds())) return {HudActionKind::Options, {}, {}};
    if (inside(x, y, repair_bounds())) return {HudActionKind::Repair, {}, {}};
    if (inside(x, y, sell_bounds())) return {HudActionKind::Sell, {}, {}};
    for (int t = 0; t < 4; ++t) if (inside(x, y, tab_bounds(t))) {
        if (std::any_of(s.choices.begin(), s.choices.end(), [&](const auto& c) { return tab_of(c) == t; })) {
            active_tab_ = t; top_row_ = 0;
        }
        return {};
    }
    if (inside(x, y, scroll_up_bounds())) { top_row_ = std::max(0, top_row_ - 1); return {}; }
    if (inside(x, y, scroll_down_bounds())) { ++top_row_; settle(s); return {}; }
    const auto list = visible(s);
    for (std::size_t i = 0; i < list.size(); ++i)
        if (inside(x, y, build_button_bounds(int(i))))
            return list[i]->enabled ? HudAction{HudActionKind::Build, list[i]->type, {}} : HudAction{};
    if (s.can_deploy && inside(x, y, deploy_bounds())) return {HudActionKind::Deploy, {}, {}};
    const auto r = minimap_bounds();
    if (inside(x, y, r) && s.map_width > 0 && s.map_height > 0 && valid(s.minimap))
        return {HudActionKind::Minimap, {},
            {static_cast<int>(static_cast<std::int64_t>(x - r.x) * s.map_width / r.width),
             static_cast<int>(static_cast<std::int64_t>(y - r.y) * s.map_height / r.height)}};
    return {};
}

bool BattleHud::same_panel(const HudState& s) const {
    const auto& old = panel_state_;
    return panel_valid_ && panel_tab_ == active_tab_ && panel_row_ == top_row_ && old.cash == s.cash &&
        old.power_output == s.power_output && old.power_drain == s.power_drain &&
        old.can_deploy == s.can_deploy && old.building_tool == s.building_tool &&
        old.choices.size() == s.choices.size() &&
        std::equal(old.choices.begin(), old.choices.end(), s.choices.begin(), [](const auto& a, const auto& b) {
            return a.type == b.type && a.name == b.name && a.cost == b.cost && a.enabled == b.enabled &&
                a.progress == b.progress && a.category == b.category && a.queued == b.queued && a.held == b.held;
        });
}

void BattleHud::draw_panel(render::Bitmap& b, const HudState& s) const {
    if (!art_.ready()) { draw_fallback_panel(b, s); return; }
    const auto layout = hud_side_layout(art_.soviet());
    const auto put = [&](const char* name, int frame, int x, int y) {
        if (const auto* sprite = art_.sprite(name, frame)) hud_blit(b, *sprite, x, y);
    };
    // Sidebar frame, top to bottom.
    put("credits.shp", 0, sidebar_x, 0);
    put("top.shp", 0, sidebar_x, 16);
    put("diplobtn.shp", 0, layout.diplomacy.x, layout.diplomacy.y);
    put("optbtn.shp", 0, layout.options.x, layout.options.y);
    put("side1.shp", 0, sidebar_x, 158);
    put("repair.shp", s.building_tool == BuildingTool::Repair ? 1 : 0, layout.repair.x, layout.repair.y);
    put("sell.shp", s.building_tool == BuildingTool::Sell ? 1 : 0, layout.sell.x, layout.sell.y);
    int count[4] = {};
    for (const auto& c : s.choices) ++count[tab_of(c)];
    for (int t = 0; t < 4; ++t) {
        const auto r = tab_bounds(t);
        const std::string name = "tab0" + std::to_string(t) + ".shp";
        put(name.c_str(), !count[t] ? 2 : t == active_tab_ ? 1 : 0, r.x, r.y);
    }
    for (int row = 0; row < build_rows; ++row) put("side2.shp", 0, sidebar_x, strip_y + row * build_button_pitch_y);
    put("side3.shp", 0, sidebar_x, 727);
    put("addon.shp", 0, sidebar_x, 753);
    const int rows = (count[active_tab_] + build_columns - 1) / build_columns;
    put("r-dn.shp", top_row_ + build_rows < rows ? 0 : 2, scroll_down_bounds().x, scroll_down_bounds().y);
    put("r-up.shp", top_row_ > 0 ? 0 : 2, scroll_up_bounds().x, scroll_up_bounds().y);
    // Credits, centered in CREDITS.SHP.
    if (const auto* font = art_.font()) {
        const auto cash = std::to_string(std::max(0, s.cash));
        hud_text(b, *font, cash, sidebar_x + (sidebar_width - hud_text_width(*font, cash)) / 2, 2, side_text);
    }
    // Power bar. TODO-ORIGINAL: the segment scale and the colour split are
    // fitted to two original screenshots, not read from the original code.
    const int lit = std::clamp(s.power_output * 2 / 7, 0, power_segments);
    const bool low = s.power_drain > s.power_output;
    for (int k = 0; k < power_segments; ++k) {
        int frame = 0;
        if (k < lit) frame = low ? 3 : k < lit / 4 ? 3 : k < lit * 3 / 4 ? 2 : 1;
        put("powerp.shp", frame, power_x, power_bottom - 3 * k);
    }
    // Cameos.
    const auto list = visible(s);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& c = *list[i];
        const auto r = build_button_bounds(int(i));
        if (const auto* image = cameo(c.type)) {
            const int shade = c.enabled ? 5 : 2;
            for (int y = 0; y < std::min(r.height, image->height); ++y)
                for (int x = 0; x < std::min(r.width, image->width); ++x) {
                    const auto* p = &image->rgba[(std::size_t(y) * image->width + x) * 4];
                    if (p[3]) pixel(b, r.x + x, r.y + y, {std::uint8_t(p[0] * shade / 5),
                        std::uint8_t(p[1] * shade / 5), std::uint8_t(p[2] * shade / 5), 255});
                }
        } else if (const auto* font = art_.font()) {
            hud_text(b, *font, c.name.empty() ? c.type : c.name, r.x + 2, r.y + 2, side_text);
        }
        // Build clock: GCLOCK2.SHP frame 1 (just started) .. 54 (almost done).
        if (c.progress > 0 && c.progress < 1000)
            if (const auto* clock = art_.sprite("gclock2.shp", 1 + std::clamp(c.progress, 0, 999) * 54 / 1000))
                hud_blend(b, *clock, r.x, r.y);
        if (const auto* font = art_.font()) {
            std::string label = c.held ? "On Hold" :
                c.progress >= 1000 && tab_of(c) <= 1 ? "Ready" : "";
            if (!label.empty()) {
                const int w = hud_text_width(*font, label);
                const int x = r.x + (r.width - w) / 2;
                fill(b, {x - 2, r.y + 1, w + 3, int(font->line_height()) + 1}, {0, 0, 0, 255});
                hud_text(b, *font, label, x, r.y + 1, side_text);
            }
            if (c.queued > 1) {
                const auto n = std::to_string(c.queued);
                hud_text(b, *font, n, r.x + r.width - hud_text_width(*font, n) - 2, r.y + 1, side_text);
            }
        }
    }
    // Command bar.
    put("lspacer.shp", 0, 0, command_bar_y);
    put("lendcap.shp", 0, 20, command_bar_y);
    for (int i = 0; i < command_slots; ++i) {
        const int x = command_slot_x + i * command_slot_width;
        if (i < int(std::size(command_buttons))) put(command_buttons[i], 0, x, command_bar_y);
        else put("bttnbkgd.shp", 0, x, command_bar_y);
    }
    put("rendcap.shp", 0, command_slot_x + command_slots * command_slot_width, command_bar_y);
}

void BattleHud::draw_fallback_panel(render::Bitmap& b, const HudState& s) const {
    // Same regions as the original art, in plain shapes.
    fill(b, {sidebar_x, 0, sidebar_width, view_height}, dark);
    fill(b, {0, command_bar_y, sidebar_x, command_bar_height}, dark);
    tiny_text(b, sidebar_x + 50, 4, "$" + std::to_string(s.cash), ink, 110, 2);
    border(b, minimap_bounds(), edge);
    const std::pair<render::Rectangle, const char*> buttons[] = {
        {options_bounds(), "OPTIONS"}, {repair_bounds(), "REPAIR"}, {sell_bounds(), "SELL"}};
    for (const auto& button : buttons) {
        border(b, button.first, ink);
        tiny_text(b, button.first.x + 2, button.first.y + 8, button.second, ink, button.first.width - 4);
    }
    int count[4] = {};
    for (const auto& c : s.choices) ++count[tab_of(c)];
    const char* names[] = {"BLD", "DEF", "INF", "VEH"};
    for (int t = 0; t < 4; ++t) {
        const auto r = tab_bounds(t);
        border(b, r, t == active_tab_ ? ink : edge);
        tiny_text(b, r.x + 2, r.y + 10, names[t], count[t] ? ink : edge, r.width - 2);
    }
    const auto list = visible(s);
    for (int i = 0; i < slots; ++i) border(b, build_button_bounds(i), edge);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& c = *list[i];
        const auto r = build_button_bounds(int(i));
        const Color color = c.enabled ? ink : edge;
        tiny_text(b, r.x + 3, r.y + 4, c.name.empty() ? c.type : c.name, color, r.width - 6);
        tiny_text(b, r.x + 3, r.y + 14, "$" + std::to_string(c.cost), color, r.width - 6);
        tiny_text(b, r.x + 3, r.y + 24, c.held ? "HOLD" : c.progress >= 1000 ? "READY" :
            c.progress > 0 ? std::to_string(c.progress / 10) + "%" : "", color, r.width - 6);
        if (c.queued > 1) tiny_text(b, r.x + 3, r.y + 34, std::to_string(c.queued), color, r.width - 6);
    }
    for (int i = 0; i < command_slots; ++i)
        border(b, {command_slot_x + i * command_slot_width, command_bar_y, command_slot_width, command_bar_height}, edge);
    if (s.can_deploy) tiny_text(b, deploy_bounds().x + 4, deploy_bounds().y + 13, "DEPLOY", ink, 48);
}

void BattleHud::draw(render::Bitmap& b, const HudState& s) const {
    loop::profile::Timer probe(loop::profile::Phase::HudDraw);
    if (!valid(b)) return;
    settle(s);
    if (!same_panel(s)) {
        if (panel_.rgba.empty()) panel_ = {view_width, view_height, render::Bytes(std::size_t(view_width) * view_height * 4)};
        draw_panel(panel_, s);
        panel_state_.cash = s.cash; panel_state_.power_output = s.power_output; panel_state_.power_drain = s.power_drain;
        panel_state_.choices = s.choices; panel_state_.can_deploy = s.can_deploy;
        panel_state_.building_tool = s.building_tool;
        panel_tab_ = active_tab_; panel_row_ = top_row_; panel_valid_ = true;
    }
    copy_rect(panel_, b, {sidebar_x, 0, sidebar_width, view_height});
    copy_rect(panel_, b, {0, command_bar_y, sidebar_x, command_bar_height});
    const auto* font = art_.font();

    // Radar: eagle logo while off; opens over 32 frames when it comes online.
    const bool radar_on = valid(s.minimap);
    // 0062F580 waits four ticks. 0069A430 is timeGetTime() >> 4.
    const auto tick = s.clock_ms >> 4;
    if (radar_online_ != radar_on) { radar_online_ = radar_on; radar_tick_.reset(); }
    if (!radar_tick_ || tick < *radar_tick_ || tick - *radar_tick_ >= 4) {
        radar_frame_ = std::clamp(radar_frame_ + (radar_on ? 1 : -1), 0, 32);
        radar_tick_ = tick;
    }
    const auto r = minimap_bounds();
    if (const auto* radar = art_.sprite("radar.shp", radar_frame_)) hud_blit(b, *radar, sidebar_x, 48);
    else fill(b, r, {8, 15, 12, 255});
    if (radar_on && (radar_frame_ == 32 || !art_.ready())) {
        for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * s.minimap.height / r.height * s.minimap.width +
                static_cast<std::size_t>(x) * s.minimap.width / r.width) * 4;
            pixel(b, r.x + x, r.y + y, {s.minimap.rgba[i], s.minimap.rgba[i + 1], s.minimap.rgba[i + 2], 255});
        }
        if (s.map_width > 0 && s.map_height > 0 && s.camera.width > 0 && s.camera.height > 0) {
            auto scale = [](std::int64_t v, int map, int size) { return static_cast<int>(std::clamp<std::int64_t>(v, 0, map) * size / map); };
            const int x = scale(s.camera.x, s.map_width, r.width), y = scale(s.camera.y, s.map_height, r.height);
            const int right = scale(static_cast<std::int64_t>(s.camera.x) + s.camera.width, s.map_width, r.width);
            const int bottom = scale(static_cast<std::int64_t>(s.camera.y) + s.camera.height, s.map_height, r.height);
            if (right > x && bottom > y) border(b, {r.x + x, r.y + y, right - x, bottom - y}, ink);
        }
    }

    // Hover: a light frame on the cameo under the pointer, and a tooltip
    // with the name and the cost.
    const auto list = visible(s);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto box = build_button_bounds(int(i));
        if (!inside(s.pointer_x, s.pointer_y, box)) continue;
        border(b, box, {side_text.r, side_text.g, side_text.b, 255});
        const auto& c = *list[i];
        const std::string name = c.name.empty() ? c.type : c.name, cost = "$" + std::to_string(c.cost);
        if (font) {
            const int w = std::max(hud_text_width(*font, name), hud_text_width(*font, cost)) + 6;
            const int h = 2 * int(font->line_height()) + 4;
            const Rectangle tip{std::max(0, sidebar_x - w - 2), std::clamp(box.y, 0, command_bar_y - h), w, h};
            fill(b, tip, {0, 0, 0, 255}); border(b, tip, {side_text.r, side_text.g, side_text.b, 255});
            hud_text(b, *font, name, tip.x + 3, tip.y + 2, side_text);
            hud_text(b, *font, cost, tip.x + 3, tip.y + 2 + int(font->line_height()), side_text);
        } else {
            const int w = 4 * int(cost.size()) + 6;
            const Rectangle tip{sidebar_x - w - 2, box.y, w, 11};
            fill(b, tip, dark); border(b, tip, edge);
            tiny_text(b, tip.x + 3, tip.y + 3, cost, ink, w - 4);
        }
        break;
    }

    // Message line, top left of the battlefield, like original game messages.
    if (!s.status.empty()) {
        if (font) shadow_text(b, *font, s.status, 8, 8, {255, 255, 255, 255});
        else tiny_text(b, 8, 8, s.status, ink, sidebar_x - 16, 2);
    }

    // End banner: GRFXTXT.SHP "You Are Victorious!" or "Game Over".
    if (!s.result.empty()) {
        const bool win = s.result.find("VICT") != std::string::npos;
        const auto* banner = art_.sprite("grfxtxt.shp", win ? 2 : 3);
        if (banner) {
            hud_blit(b, *banner, (sidebar_x - banner->width) / 2, (command_bar_y - banner->height) / 2,
                     {0, 0, sidebar_x, command_bar_y});
        } else {
            const Rectangle box{(sidebar_x - 604) / 2, 330, 604, 60};
            fill(b, box, dark); border(b, box, ink);
            const int scale = 8, w = static_cast<int>(s.result.size()) * 4 * scale - scale;
            tiny_text(b, box.x + std::max(0, (box.width - w) / 2), box.y + (box.height - 5 * scale) / 2,
                      s.result, ink, box.width - 16, scale);
        }
    }
}
} // namespace ra2::app
