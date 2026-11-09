#include "sidebar_choices.h"
#include "battle_hud.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../gfx/shp_decode.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace ra2;
using namespace ra2::app;
namespace {
render::Bitmap frame(int w = 1024, int h = 768) {
    return {w, h, render::Bytes(static_cast<std::size_t>(w) * h * 4, 17)};
}
bool same(const render::Bitmap& a, const render::Bitmap& b, render::Rectangle r) {
    for (int y = r.y; y < r.y + r.height; ++y) for (int x = r.x; x < r.x + r.width; ++x)
        for (int c = 0; c < 4; ++c) if (a.rgba[(y * a.width + x) * 4 + c] != b.rgba[(y * b.width + x) * 4 + c]) return false;
    return true;
}
bool rect_eq(render::Rectangle a, render::Rectangle b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
int count_rgb(const render::Bitmap& img, render::Rectangle box, int r, int g, int b) {
    int hits = 0;
    for (int y = box.y; y < box.y + box.height; ++y) for (int x = box.x; x < box.x + box.width; ++x) {
        const auto i = (static_cast<std::size_t>(y) * img.width + x) * 4;
        if (img.rgba[i] == r && img.rgba[i + 1] == g && img.rgba[i + 2] == b) ++hits;
    }
    return hits;
}
// Independent decode of one original sidebar SHP frame, straight from the side MIX.
gfx::RgbaImage side_frame(const fs::MixFileSystem& files, const char* mix, const char* shp, int index) {
    const auto at = files.locate(mix); assert(at);
    const auto archive = at->archive->open_nested(at->entry, mix);
    const auto* pal = archive->find("sidebar.pal"); assert(pal);
    const auto palette = gfx::Palette::from_bytes(archive->read(*pal));
    const auto* entry = archive->find(shp);
    auto bytes = entry ? archive->read(*entry) : *files.read_file(shp);
    return gfx::ShpImage::parse(bytes).decode_frame(index, palette);
}
// Counts the opaque pixels of the original frame that show unchanged at (x, y).
int exact(const render::Bitmap& screen, const gfx::RgbaImage& image, int x, int y, int max_y = 768) {
    int total = 0;
    for (int yy = 0; yy < image.height; ++yy) for (int xx = 0; xx < image.width; ++xx) {
        if (y + yy >= max_y) continue;
        const auto* p = &image.pixels[(std::size_t(yy) * image.width + xx) * 4];
        if (!p[3]) continue;
        const auto* d = &screen.rgba[(std::size_t(y + yy) * screen.width + x + xx) * 4];
        total += d[0] == p[0] && d[1] == p[1] && d[2] == p[2];
    }
    return total;
}
}

int main(int argc, char** argv) {
    // Power loss cannot move a tank button onto a different product.
    SidebarChoices stable;
    auto initial = stable.update({{"GAREFN", "REFINERY", 2000, true, 0}, {"MTNK", "GRIZZLY", 700, true, 0}});
    assert(initial.size() == 2 && initial[1].type == "MTNK");
    auto low_power = stable.update({{"MTNK", "GRIZZLY", 700, true, 300}});
    assert(low_power.size() == 2 && !low_power[0].enabled && low_power[1].type == "MTNK");
    assert(low_power[1].progress == 300);

    rules::IniFile order_rules;
    order_rules.parse("[VehicleTypes]\n0=BOAT\n1=AIR\n2=TANK\n"
        "[BOAT]\nNaval=yes\nTechLevel=1\n[AIR]\nConsideredAircraft=yes\nTechLevel=1\n"
        "[TANK]\nTechLevel=9\n");
    SidebarChoices domains;domains.use_rules(order_rules,0);
    const auto domain_order=domains.update({{"BOAT","",1,true},{"AIR","",2,true},{"TANK","",1000,true}});
    assert(domain_order[0].type=="TANK" && domain_order[1].type=="AIR" && domain_order[2].type=="BOAT");

    // Original 1024x768 layout: 168 pixel sidebar, 2x10 cameos of 60x48,
    // radar screen at the top, command bar under the battlefield.
    static_assert(BattleHud::sidebar_x == 856 && BattleHud::command_bar_y == 736);
    assert(rect_eq(BattleHud::build_button_bounds(0), {878, 228, 60, 48}));
    assert(rect_eq(BattleHud::build_button_bounds(1), {941, 228, 60, 48}));
    assert(rect_eq(BattleHud::build_button_bounds(19), {941, 678, 60, 48}));
    assert(BattleHud::build_button_bounds(20).width == 0);
    assert(rect_eq(BattleHud::minimap_bounds(), {868, 48, 142, 110}));
    assert(rect_eq(BattleHud::deploy_bounds(), {204, 736, 52, 32}));

    BattleHud hud; HudState s;
    s.choices = {{"GAPOWR", "Power Plant", 800, true, 0, BuildCategory::Buildings},
                 {"GAREFN", "Ore Refinery", 2000, false, 0, BuildCategory::Buildings}};
    auto r = BattleHud::build_button_bounds(0);
    assert(hud.handle_click(r.x, r.y, s).type == "GAPOWR");
    assert(hud.handle_click(r.x + r.width - 1, r.y + r.height - 1, s).kind == HudActionKind::Build);
    assert(hud.handle_click(r.x + r.width, r.y, s).kind == HudActionKind::None); // Gap between cameos.
    auto r1 = BattleHud::build_button_bounds(1);
    assert(hud.handle_click(r1.x + 5, r1.y + 5, s).kind == HudActionKind::None); // Disabled.
    assert(rect_eq(hud.choice_bounds(s, "GAREFN"), r1));

    // Top Options and the building tools use their original side art bounds.
    const std::pair<render::Rectangle, HudActionKind> buttons[] = {
        {hud.options_bounds(), HudActionKind::Options}, {hud.repair_bounds(), HudActionKind::Repair},
        {hud.sell_bounds(), HudActionKind::Sell}};
    for(const auto& item : buttons) {
        assert(hud.handle_click(item.first.x, item.first.y, s).kind==item.second);
        assert(hud.handle_click(item.first.x+item.first.width-1, item.first.y+item.first.height-1, s).kind==item.second);
    }

    // Deploy is the fourth command bar button and works only when allowed.
    const auto d = BattleHud::deploy_bounds();
    assert(hud.handle_click(d.x + 8, d.y + 8, s).kind == HudActionKind::None);
    s.can_deploy = true;
    assert(hud.handle_click(d.x + 8, d.y + 8, s).kind == HudActionKind::Deploy);
    assert(hud.handle_click(d.x - 1, d.y + 8, s).kind == HudActionKind::None);
    s.can_deploy = false;

    // Radar clicks need a live radar image and a map size.
    const auto m = BattleHud::minimap_bounds();
    s.map_width = 1420; s.map_height = 1100;
    assert(hud.handle_click(m.x + 71, m.y + 55, s).kind == HudActionKind::None);
    s.minimap = {2, 1, {10, 20, 30, 255, 40, 50, 60, 255}};
    auto a = hud.handle_click(m.x + 71, m.y + 55, s);
    assert(a.kind == HudActionKind::Minimap && a.position.x == 710 && a.position.y == 550);
    assert(hud.handle_click(m.x + m.width, m.y, s).kind == HudActionKind::None);

    // Four tabs: buildings, defenses (BuildCat=Combat), infantry, vehicles
    // with aircraft. Empty tabs ignore clicks.
    HudState tabs;
    tabs.choices = {{"GAPOWR", "Power Plant", 800, true, 0, BuildCategory::Buildings},
                    {"GAPILL", "Pillbox", 500, true, 0, BuildCategory::Defenses},
                    {"E1", "GI", 200, true, 0, BuildCategory::Infantry},
                    {"ORCA", "Harrier", 1200, true, 0, BuildCategory::Aircraft}};
    BattleHud tabbed;
    assert(tabbed.active_tab() == 0 && tabbed.handle_click(r.x + 1, r.y + 1, tabs).type == "GAPOWR");
    const auto t2 = tabbed.tab_bounds(2);
    assert(rect_eq(t2, {882 + 2 * 29, 197, 28, 27}));
    tabbed.handle_click(t2.x + 1, t2.y + 1, tabs);
    assert(tabbed.handle_click(r.x + 1, r.y + 1, tabs).type == "E1");
    tabbed.handle_click(tabbed.tab_bounds(1).x + 1, 200, tabs);
    assert(tabbed.handle_click(r.x + 1, r.y + 1, tabs).type == "GAPILL");
    tabbed.handle_click(tabbed.tab_bounds(3).x + 1, 200, tabs);
    assert(tabbed.handle_click(r.x + 1, r.y + 1, tabs).type == "ORCA");
    HudState no_vehicles = tabs; no_vehicles.choices.pop_back();
    tabbed.handle_click(tabbed.tab_bounds(0).x + 1, 200, no_vehicles);
    tabbed.handle_click(tabbed.tab_bounds(3).x + 1, 200, no_vehicles); // Empty tab.
    assert(tabbed.active_tab() == 0);
    // An empty active tab moves to the first tab with items.
    HudState infantry_only; infantry_only.choices = {{"E1", "GI", 200, true, 0, BuildCategory::Infantry}};
    BattleHud fresh;
    assert(fresh.handle_click(r.x + 1, r.y + 1, infantry_only).type == "E1" && fresh.active_tab() == 2);

    // More than twenty cameos scroll by one row with the original arrows.
    {
        BattleHud pages; HudState many;
        for (int i = 0; i < 25; ++i)
            many.choices.push_back({"TYPE" + std::to_string(i), "Item", 100, true, 0, BuildCategory::Buildings});
        auto image = frame(); pages.draw(image, many);
        assert(pages.handle_click(r.x + 1, r.y + 1, many).type == "TYPE0");
        assert(rect_eq(pages.choice_bounds(many, "TYPE20"), {}));
        const auto down = BattleHud::scroll_down_bounds(), up = BattleHud::scroll_up_bounds();
        pages.handle_click(down.x + 2, down.y + 2, many);
        assert(pages.handle_click(r.x + 1, r.y + 1, many).type == "TYPE2");
        for (int i = 0; i < 5; ++i) pages.handle_click(down.x + 2, down.y + 2, many);
        assert(pages.handle_click(r.x + 1, r.y + 1, many).type == "TYPE6"); // Last row is in view.
        assert(rect_eq(pages.choice_bounds(many, "TYPE24"), BattleHud::build_button_bounds(18)));
        pages.handle_click(up.x + 2, up.y + 2, many);
        assert(pages.handle_click(r.x + 1, r.y + 1, many).type == "TYPE4");
        many.choices.resize(3); pages.draw(image, many);
        assert(pages.handle_click(r.x + 1, r.y + 1, many).type == "TYPE0");
    }

    // Fallback drawing without the original art: only HUD regions change.
    {
        BattleHud plain; HudState st = s;
        auto blank = frame(), b = blank; plain.draw(b, st);
        assert(same(blank, b, {0, 0, 856, 736}));
        assert(!same(blank, b, {856, 0, 168, 768}) && !same(blank, b, {0, 736, 856, 32}));
        auto again = frame(); plain.draw(again, st);
        assert(again.rgba == b.rgba); // Cached panel fully covers a fresh frame.
        st.cash = 12345; auto changed = frame(); plain.draw(changed, st);
        assert(!same(b, changed, {856, 0, 168, 16}));
        auto small = frame(810, 50); plain.draw(small, st);
        assert(small.rgba.size() == 810u * 50 * 4);
        render::Bitmap malformed{1024, 768, {1, 2, 3}}; plain.draw(malformed, st);
        assert(malformed.rgba.size() == 3);
        st.result = "VICTORY"; auto banner = frame(); plain.draw(banner, st);
        assert(!same(blank, banner, {100, 330, 604, 60}));
        st.result.clear(); st.status = "LOW POWER"; auto msg = frame(); plain.draw(msg, st);
        assert(!same(blank, msg, {8, 8, 200, 12}));
    }

    if (argc > 1) {
        fs::MixFileSystem files(argv[1]);
        files.mount_language(fs::GameVariant::RedAlert2);
        files.bootstrap(fs::GameVariant::RedAlert2);
        files.mount_overlays(); files.mount_resources();
        auto read_ini = [&](const char* name) {
            auto bytes = files.read_file(name); assert(bytes);
            rules::IniFile ini;
            ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            return ini;
        };
        auto rules = read_ini("rules.ini"), art = read_ini("art.ini");
        HudState o;
        o.cash = 6700; o.power_output = 200; o.power_drain = 60;
        // Buildings tab: the root sends Pillbox as a building; rules move it to defenses.
        o.choices = {{"GAPOWR", "Power Plant", 800, true, 0, BuildCategory::Buildings},
                     {"GAPILE", "Barracks", 500, true, 0, BuildCategory::Buildings},
                     {"GAPILL", "Pillbox", 500, true, 0, BuildCategory::Buildings},
                     {"MTNK", "Grizzly Tank", 700, true, 0, BuildCategory::Vehicles}};
        // Base 0067EA20 sorts by type fields, regardless of discovery order.
        SidebarChoices ordered;ordered.use_rules(rules,0);
        const auto choices_for=[&](std::initializer_list<const char*> ids) {
            std::vector<BuildChoice> out;
            for(const auto id:ids)out.push_back({id,id,rules.read_integer(id,"Cost",0),true});
            return out;
        };
        const auto ids_of=[](const auto& choices) {
            std::vector<std::string> out;for(const auto& c:choices)out.push_back(c.type);return out;
        };
        auto vehicles=ordered.update(choices_for({"SHAD","FV","MTNK","CMIN"}));
        assert(ids_of(vehicles)==std::vector<std::string>({"CMIN","MTNK","FV","SHAD"}));
        SidebarChoices buildings;buildings.use_rules(rules,0);
        buildings.update(choices_for({"GAYARD","GAAIRC","GAWEAP"}));
        auto sorted=buildings.update(choices_for({"GAPILE","GAREFN","GAPOWR","GAYARD","GAAIRC","GAWEAP"}));
        assert(ids_of(sorted)==std::vector<std::string>({"GAPOWR","GAREFN","GAPILE","GAWEAP","GAAIRC","GAYARD"}));
        const auto retained=buildings.update(choices_for({"GAPOWR"}));
        assert(ids_of(retained)==ids_of(sorted) && !retained[3].enabled);
        BattleHud allied;
        allied.load_assets(files, rules, art, o.choices);
        assert(allied.original_art());
        assert(allied.tab_of(o.choices[2]) == 1);
        auto screen = frame(); allied.draw(screen, o);
        // The metal frame, radar eagle, tabs, strips and command bar are the
        // installed original art, pixel for pixel, at the original places.
        const char* mix = "sidec01.mix";
        assert(exact(screen, side_frame(files, mix, "radar.shp", 0), 856, 48) == 18480); // Eagle logo.
        assert(exact(screen, side_frame(files, mix, "top.shp", 0), 856, 16) > 1000);
        assert(exact(screen, side_frame(files, mix, "repair.shp", 0), 876, 166) == 1984);
        assert(exact(screen, side_frame(files, mix, "sell.shp", 0), 940, 166) == 1984);
        assert(exact(screen, side_frame(files, mix, "tab00.shp", 1), 882, 197) == 756);  // Active.
        assert(exact(screen, side_frame(files, mix, "tab01.shp", 0), 911, 197) == 756);  // Has items.
        assert(exact(screen, side_frame(files, mix, "tab02.shp", 2), 940, 197) == 756);  // Empty.
        assert(exact(screen, side_frame(files, mix, "tab03.shp", 0), 969, 197) == 756);
        assert(exact(screen, side_frame(files, mix, "side3.shp", 0), 856, 727) > 1000);
        assert(exact(screen, side_frame(files, mix, "addon.shp", 0), 856, 753) > 500);
        assert(exact(screen, side_frame(files, mix, "lendcap.shp", 0), 20, 736) == 896);
        assert(exact(screen, side_frame(files, mix, "button04.shp", 0), d.x, d.y) == 1664); // Deploy.
        assert(exact(screen, side_frame(files, mix, "bttnbkgd.shp", 0), 360, 736) == 1664);
        assert(exact(screen, side_frame(files, mix, "rendcap.shp", 0), 828, 736) == 896);
        // Empty cameo slots are the black SIDE2.SHP strips (rows 1..9 here).
        const auto strip = side_frame(files, mix, "side2.shp", 0);
        for (int row = 1; row < 10; ++row) {
            const int y = 227 + 50 * row;
            for (int yy = 0; yy < strip.height; ++yy) for (int xx = 0; xx < strip.width; ++xx) {
                if (xx < 17 || !strip.pixels[(std::size_t(yy) * strip.width + xx) * 4 + 3]) continue; // Power bar column.
                const auto* p = &strip.pixels[(std::size_t(yy) * strip.width + xx) * 4];
                const auto* q = &screen.rgba[(std::size_t(y + yy) * 1024 + 856 + xx) * 4];
                assert(p[0] == q[0] && p[1] == q[1] && p[2] == q[2]);
            }
        }
        // Credits in the original GAME.FNT blue, centred in the credits box.
        assert(count_rgb(screen, {856, 0, 168, 16}, 165, 211, 255) > 40);
        assert(count_rgb(screen, {856, 0, 84 - 12, 16}, 165, 211, 255) == 0);
        // Power bar segments: lit with output, unlit above.
        const auto lit = side_frame(files, mix, "powerp.shp", 1), dark_seg = side_frame(files, mix, "powerp.shp", 0);
        assert(exact(screen, dark_seg, 861, 725 - 3 * 166) > 0);
        assert(count_rgb(screen, {861, 400, 12, 326}, lit.pixels[0], lit.pixels[1], lit.pixels[2]) > 0);
        // Battlefield stays untouched.
        assert(same(frame(), screen, {0, 0, 856, 736}));

        // Original cameo art in slot 0, untouched at progress 0.
        const auto palette = gfx::Palette::from_bytes(*files.read_file("cameo.pal"));
        const auto icon = gfx::ShpImage::parse(*files.read_file("POWRICON.SHP")).decode_frame(0, palette);
        assert(exact(screen, icon, 878, 228) > 2800);
        // Disabled cameos are darker and refuse clicks.
        auto off = o; off.choices[0].enabled = false;
        auto dimmed = frame(); allied.draw(dimmed, off);
        int darker = 0;
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 60; ++x) {
            const auto i = (std::size_t(228 + y) * 1024 + 878 + x) * 4;
            for (int c = 0; c < 3; ++c) { assert(dimmed.rgba[i + c] <= screen.rgba[i + c]); darker += dimmed.rgba[i + c] < screen.rgba[i + c]; }
        }
        assert(darker > 1000 && allied.handle_click(880, 230, off).kind == HudActionKind::None);
        // Build clock: GCLOCK2.SHP frame 28 at half progress, a 50/50 mix.
        auto half = o; half.choices[0].progress = 500;
        auto clocked = frame(); allied.draw(clocked, half);
        const auto clock = side_frame(files, mix, "gclock2.shp", 28);
        int mixed = 0, clean = 0;
        for (int y = 0; y < 48; ++y) for (int x = 0; x < 60; ++x) {
            const auto* c = &clock.pixels[(std::size_t(y) * 60 + x) * 4];
            const auto* ic = &icon.pixels[(std::size_t(y) * 60 + x) * 4];
            const auto* q = &clocked.rgba[(std::size_t(228 + y) * 1024 + 878 + x) * 4];
            if (!ic[3]) continue;
            if (c[3]) { assert(q[0] == (ic[0] + c[0]) / 2 && q[2] == (ic[2] + c[2]) / 2); ++mixed; }
            else { assert(q[0] == ic[0] && q[1] == ic[1] && q[2] == ic[2]); ++clean; }
        }
        assert(mixed > 500 && clean > 500);
        // Ready and On Hold labels in the sidebar blue.
        auto ready = o; ready.choices[0].progress = 1000;
        auto rf = frame(); allied.draw(rf, ready);
        assert(count_rgb(rf, {878, 228, 60, 16}, 165, 211, 255) > 20);
        auto held = o; held.choices[0].progress = 300; held.choices[0].held = true;
        auto hf = frame(); allied.draw(hf, held);
        assert(!same(rf, hf, {878, 228, 60, 16}) && count_rgb(hf, {878, 228, 60, 16}, 165, 211, 255) > 20);
        // Hover: blue frame on the cameo and a tooltip left of the sidebar.
        auto hover = o; hover.pointer_x = 890; hover.pointer_y = 240;
        auto hv = frame(); allied.draw(hv, hover);
        assert(count_rgb(hv, {878, 228, 60, 1}, 165, 211, 255) == 60);
        assert(count_rgb(hv, {600, 228, 256, 40}, 165, 211, 255) > 50);
        // Radar: the eagle until the radar image arrives, then 32 opening
        // frames, then the radar image inside the frame.
        auto radar = o; radar.minimap = {2, 1, {10, 20, 30, 255, 40, 50, 60, 255}};
        radar.map_width = 1420; radar.map_height = 1100; radar.camera = {0, 0, 856, 736};
        auto opening = frame(); allied.draw(opening, radar);
        assert(exact(opening, side_frame(files, mix, "radar.shp", 1), 856, 48) > 1000);
        auto repeated=frame();allied.draw(repeated,radar);
        assert(same(opening,repeated,BattleHud::minimap_bounds()));
        radar.clock_ms=63;allied.draw(repeated,radar);
        assert(same(opening,repeated,BattleHud::minimap_bounds()));
        radar.clock_ms=64;allied.draw(repeated,radar);
        assert(exact(repeated,side_frame(files,mix,"radar.shp",2),856,48)>1000);
        for (int i = 2; i < 32; ++i) {radar.clock_ms=i*64;allied.draw(opening, radar);}
        assert(count_rgb(opening, {880, 100, 40, 20}, 10, 20, 30) == 800);
        assert(count_rgb(opening, {960, 100, 40, 20}, 40, 50, 60) == 800);
        // End banner: original "You Are Victorious!" and "Game Over" art.
        auto won = o; won.result = "VICTORY";
        auto wf = frame(); allied.draw(wf, won);
        const auto victory = gfx::ShpImage::parse(*files.read_file("grfxtxt.shp"))
            .decode_frame(2, gfx::Palette::from_bytes(*files.read_file("grfxtxt.pal")));
        assert(exact(wf, victory, (856 - 436) / 2, (736 - 112) / 2) > 20000);
        auto lost = o; lost.result = "DEFEAT";
        auto lf = frame(); allied.draw(lf, lost);
        assert(!same(wf, lf, {210, 312, 436, 112}));
        // Status message in GAME.FNT at the top left of the battlefield.
        auto told = o; told.status = "Building Power Plant.";
        auto tf = frame(); allied.draw(tf, told);
        assert(count_rgb(tf, {8, 8, 200, 16}, 255, 255, 255) > 50);

        // Soviet sidebar: SIDEC02 art with its own tab and button places.
        BattleHud soviet;
        soviet.load_assets(files, rules, art, o.choices, true);
        assert(soviet.original_art() && rect_eq(soviet.tab_bounds(1), {908, 197, 32, 28}));
        auto sv = frame(); soviet.draw(sv, o);
        assert(exact(sv, side_frame(files, "sidec02.mix", "radar.shp", 0), 856, 48) > 18000);
        assert(exact(sv, side_frame(files, "sidec02.mix", "repair.shp", 0), 889, 165) > 1600);
        assert(exact(sv, side_frame(files, "sidec02.mix", "tab00.shp", 1), 876, 197) > 800);
        for(BattleHud* side : {&allied, &soviet}) {
            assert(side->handle_click(side->options_bounds().x+2, side->options_bounds().y+2, o).kind==HudActionKind::Options);
            assert(side->handle_click(side->repair_bounds().x+2, side->repair_bounds().y+2, o).kind==HudActionKind::Repair);
            assert(side->handle_click(side->sell_bounds().x+2, side->sell_bounds().y+2, o).kind==HudActionKind::Sell);
            auto active=o; active.building_tool=BuildingTool::Repair;
            auto active_frame=frame(); side->draw(active_frame,active);
            const auto r=side->repair_bounds();
            assert(exact(active_frame, side_frame(files, side==&allied?"sidec01.mix":"sidec02.mix", "repair.shp",1), r.x,r.y)>1000);
            side->draw(active_frame,o);
            assert(exact(active_frame, side_frame(files, side==&allied?"sidec01.mix":"sidec02.mix", "repair.shp",0), r.x,r.y)>1000);
        }
        assert(soviet.handle_click(910, 200, o).kind == HudActionKind::None && soviet.active_tab() == 1);
    }
    std::cout << "battle HUD tests passed\n";
}
