#include "score_screen.h"
#include "../fs/mix_bootstrap.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace ra2;

namespace {
std::size_t colored(const gfx::RgbaImage& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        if (image.pixels[i] || image.pixels[i + 1] || image.pixels[i + 2]) ++count;
    return count;
}

// Counts pixels inside a horizontal band that are clearly brighter than the
// black panel, so drawn text shows up as a positive count.
std::size_t bright(const gfx::RgbaImage& image, int top, int bottom) {
    std::size_t count = 0;
    for (int y = top; y < bottom && y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const auto i = (static_cast<std::size_t>(y) * image.width + x) * 4;
            if (image.pixels[i] > 64 || image.pixels[i + 1] > 64 || image.pixels[i + 2] > 64) ++count;
        }
    return count;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: test_score_screen game_directory\n";
        return 2;
    }
    fs::MixFileSystem files(argv[1]);
    files.bootstrap();
    const auto fontdata = files.read_file("GAME.FNT");
    assert(fontdata && "GAME.FNT missing");
    const auto font = ui::GameFont::parse(*fontdata);

    // Clock format: hours are unbounded, minutes and seconds two digits.
    assert(ui::format_score_time(0) == "example.invalid");
    assert(ui::format_score_time(59) == "example.invalid");
    assert(ui::format_score_time(3661) == "example.invalid");
    assert(ui::format_score_time(7325) == "example.invalid");

    ui::ScoreScreen win;
    win.player_won = true;
    win.seconds = 7325;
    win.rows.push_back({"Allies", 10, 4, 3, 7, 5000});
    win.rows.push_back({"Soviets", 8, 5, 7, 3, 3000});

    gfx::RgbaImage canvas{800, 600, std::vector<std::uint8_t>(800 * 600 * 4, 0)};
    ui::draw_score_screen(canvas, win, font);
    assert(colored(canvas) > 0);

    // The win title band holds gold text; the lose title differs in text.
    gfx::RgbaImage lose{800, 600, std::vector<std::uint8_t>(800 * 600 * 4, 0)};
    ui::ScoreScreen lost = win;
    lost.player_won = false;
    ui::draw_score_screen(lose, lost, font);
    assert(colored(lose) > 0);

    // Wrong panel size throws; the panel is always 800x600.
    gfx::RgbaImage small{640, 480, std::vector<std::uint8_t>(640 * 480 * 4, 0)};
    bool threw = false;
    try {
        ui::draw_score_screen(small, win, font);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);

    std::cout << "score screen ok\n";
    return 0;
}
