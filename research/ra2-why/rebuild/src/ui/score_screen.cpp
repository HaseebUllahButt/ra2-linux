#include "score_screen.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace ra2::ui {
namespace {
// Original score screen palette. The briefing screens draw gold headings and
// white values on black; the score screen reuses that scheme.
constexpr gfx::Rgba kTitleGold{240, 208, 96, 255};
constexpr gfx::Rgba kTextWhite{224, 224, 224, 255};
constexpr gfx::Rgba kTextDim{144, 144, 144, 255};
constexpr gfx::Rgba kPanelBlack{0, 0, 0, 255};

// Column layout in 800x600 client pixels. The original dialog places the
// table left edge near 0x30; TODO-ORIGINAL: the exact column x positions
// come from the resource editor, not recovered here.
constexpr int kTitleTop = 24;
constexpr int kTimeTop = 56;
constexpr int kTableTop = 112;
constexpr int kRowHeight = 24;
constexpr int kNameX = 40;
constexpr int kColumnX = 240;  // First stat column; columns are 120 apart.
constexpr int kColumnWidth = 120;

void draw_centered(gfx::RgbaImage& image, const GameFont& font, const std::string& text,
                   int center_x, int y, gfx::Rgba color) {
    const auto width = font.text_width(text);
    font.draw_text(image, text, center_x - static_cast<int>(width) / 2, y, color);
}
} // namespace

std::string format_score_time(std::uint32_t seconds) {
    const std::uint32_t hours = seconds / 3600;
    const std::uint32_t minutes = (seconds / 60) % 60;
    const std::uint32_t secs = seconds % 60;
    char buffer[16 + 1 + 2 + 1 + 2 + 1];
    std::snprintf(buffer, sizeof(buffer), "%u:%02u:%02u", hours, minutes, secs);
    return buffer;
}

void draw_score_screen(gfx::RgbaImage& target, const ScoreScreen& screen,
                       const GameFont& font) {
    if (screen.rows.empty()) throw std::runtime_error("Score screen needs at least one house row");
    // Paint over any previous frame; the caller reuses one buffer per match.
    std::fill(target.pixels.begin(), target.pixels.end(), std::uint8_t{0});
    for (std::size_t i = 3; i < target.pixels.size(); i += 4) target.pixels[i] = 255;

    // Result headline first: gold win or lose text, then the match length.
    draw_centered(target, font, screen.player_won ? "MISSION ACCOMPLISHED" : "MISSION FAILED",
                  static_cast<int>(target.width) / 2, kTitleTop, kTitleGold);
    draw_centered(target, font, "TIME " + format_score_time(screen.seconds),
                  static_cast<int>(target.width) / 2, kTimeTop, kTextDim);

    // Column headers.
    const int header_y = kTableTop;
    font.draw_text(target, "HOUSE", kNameX, header_y, kTextWhite);
    font.draw_text(target, "UNITS BUILT", kColumnX, header_y, kTextWhite);
    font.draw_text(target, "BUILDINGS", kColumnX + 120, header_y, kTextWhite);
    font.draw_text(target, "UNITS LOST", kColumnX + 240, header_y, kTextWhite);
    font.draw_text(target, "UNITS KILLED", kColumnX + 360, header_y, kTextWhite);
    font.draw_text(target, "CREDITS", kColumnX + 480, header_y, kTextWhite);

    // One row per house: label, then the five stat columns.
    int y = kTableTop + kRowHeight;
    for (const auto& side : screen.rows) {
        font.draw_text(target, side.name, kNameX, y, kTextWhite);
        font.draw_text(target, std::to_string(side.units_built),
                       kColumnX + kColumnWidth - 10 - font.text_width(std::to_string(side.units_built)), y, kTextWhite);
        font.draw_text(target, std::to_string(side.buildings_built),
                       kColumnX + 2 * kColumnWidth - 10 - font.text_width(std::to_string(side.buildings_built)), y, kTextWhite);
        font.draw_text(target, std::to_string(side.units_lost),
                       kColumnX + 3 * kColumnWidth - 10 - font.text_width(std::to_string(side.units_lost)), y, kTextWhite);
        font.draw_text(target, std::to_string(side.units_killed),
                       kColumnX + 4 * kColumnWidth - 10 - font.text_width(std::to_string(side.units_killed)), y, kTextWhite);
        font.draw_text(target, std::to_string(side.credits_harvested),
                       kColumnX + 5 * kColumnWidth - 10 - font.text_width(std::to_string(side.credits_harvested)), y, kTextWhite);
        y += kRowHeight;
    }
}
} // namespace ra2::ui
