#include "score_screen.h"
#include "../fs/mix_bootstrap.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ra2;
namespace {
void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(std::string("FAILED: ") + what);
}
std::size_t bright(const gfx::RgbaImage& im, int x0, int y0, int x1, int y1) {
    std::size_t n = 0;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        auto i = (std::size_t(y) * im.width + x) * 4;
        if (im.pixels[i] > 100 || im.pixels[i + 1] > 100 || im.pixels[i + 2] > 100) ++n;
    }
    return n;
}
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: test_score_screen game_dir\n"; return 2; }
    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    ra2text::CsfFile strings;
    auto csf = files.read_file("ra2.csf");
    check(csf.has_value(), "ra2.csf");
    strings.parse(csf->data(), csf->size());
    auto assets = ui::ScoreAssets::load(files, strings);
    check(assets.background.width == 640 && assets.background.height == 400, "original art loaded");
    check(assets.title_win == "You Are Victorious!", "win title from CSF");
    check(assets.title_lose == "Mission Failed", "lose title from CSF");
    check(assets.built == "Built" && assets.harvested == "Harvested", "labels from CSF");

    check(ui::format_score_time(0) == "example.invalid", "clock 0");
    check(ui::format_score_time(3661) == "example.invalid", "clock hour");
    check(ui::format_score_time(7325) == "example.invalid", "clock 2h");

    ui::ScoreTally t({"Allies", "Soviet"});
    t.built(0, false); t.built(0, false); t.built(0, true); t.built(1, true);
    t.harvested(0, 700); t.harvested(0, 300);
    t.destroyed(0, 1); t.destroyed(0, 1); t.destroyed(1, 0);
    t.destroyed(ui::ScoreTally::npos, 0);   // death with no killer
    t.destroyed(1, 1);                      // own-house kill
    t.built(9, true);                       // out of range: ignored
    auto win = t.finish(true, 3725);
    check(win.houses[0].units_built == 2 && win.houses[0].buildings_built == 1, "built counts");
    check(win.houses[0].credits_harvested == 1000, "harvest");
    check(win.houses[0].kills() == 2 && win.houses[0].kills_of[1] == 2, "kills");
    check(win.houses[0].losses() == 2 && win.houses[0].losses_to[1] == 1, "losses");
    check(win.houses[1].kills() == 1 && win.houses[1].losses() == 3, "enemy totals");

    auto a = ui::draw_score_screen(assets, win);
    check(a.width == 800 && a.height == 600 && a.pixels.size() == 800 * 600 * 4, "size");
    check(a.pixels[3] == 255, "opaque");
    // Title row (just under the heading) differs between win and lose.
    auto lose_screen = win; lose_screen.player_won = false;
    auto b = ui::draw_score_screen(assets, lose_screen);
    check(a.pixels != b.pixels, "win and lose differ");
    check(bright(a, 0, 100, 800, 500) > 200, "table text drawn");
    // Art shows: the original picture has colour outside the black border.
    check(bright(a, 80, 100, 720, 500) > 0, "art or text inside panel");
    // Black fallback still draws text.
    ui::ScoreAssets bare = assets; bare.background = {};
    auto c = ui::draw_score_screen(bare, win);
    check(bright(c, 0, 100, 800, 500) > 200, "text without art");
    check(a.pixels != c.pixels, "art changes image");
    // Bad inputs.
    bool threw = false;
    try { ui::draw_score_screen(assets, ui::ScoreScreen{}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "empty throws");
    if (const char* dump = std::getenv("SCORE_DUMP")) {
        std::ofstream o(dump, std::ios::binary);
        o << "P6\n800 600\n255\n";
        for (std::size_t i = 0; i < a.pixels.size(); i += 4) o.write(reinterpret_cast<const char*>(&a.pixels[i]), 3);
    }
    std::cout << "score screen ok\n";
}
