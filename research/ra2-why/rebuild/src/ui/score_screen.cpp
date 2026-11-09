#include "score_screen.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <stdexcept>

namespace ra2::ui {
std::uint32_t ScoreHouse::kills() const noexcept {
    return std::accumulate(kills_of.begin(), kills_of.end(), 0u);
}
std::uint32_t ScoreHouse::losses() const noexcept {
    return std::accumulate(losses_to.begin(), losses_to.end(), 0u);
}

ScoreTally::ScoreTally(std::vector<std::string> names) {
    const auto count = names.size();
    for (auto& name : names) {
        ScoreHouse h;
        h.name = std::move(name);
        h.kills_of.assign(count, 0);
        h.losses_to.assign(count, 0);
        state_.houses.push_back(std::move(h));
    }
}
void ScoreTally::built(std::size_t house, bool is_building) {
    if (house >= state_.houses.size()) return;
    (is_building ? state_.houses[house].buildings_built : state_.houses[house].units_built)++;
}
void ScoreTally::harvested(std::size_t house, std::uint32_t credits) {
    if (house < state_.houses.size()) state_.houses[house].credits_harvested += credits;
}
void ScoreTally::destroyed(std::size_t killer, std::size_t victim) {
    const auto n = state_.houses.size();
    if (victim >= n) return;
    // TODO-ORIGINAL 0x005C5FB0 area (HouseClass kill counters): the original
    // counts unit and building kills apart and skips own-house kills. This
    // version counts every death once; self-kills count as a loss only.
    state_.houses[victim].losses_to[killer < n ? killer : victim]++;
    if (killer < n && killer != victim) state_.houses[killer].kills_of[victim]++;
}
ScoreScreen ScoreTally::finish(bool player_won, std::uint32_t seconds) const {
    ScoreScreen out = state_;
    out.player_won = player_won;
    out.seconds = seconds;
    return out;
}

ScoreAssets ScoreAssets::load(const fs::MixFileSystem& files, const ra2text::CsfFile& strings) {
    ScoreAssets a;
    auto font = files.read_file("GAME.FNT");
    if (!font) throw std::runtime_error("GAME.FNT missing");
    a.font = GameFont::parse(std::move(*font));
    if (auto pcx = files.read_file("mpscore.pcx")) a.background = decode_pcx(*pcx);
    auto text = [&](const char* label, std::string& out) {
        if (const auto* v = strings.find(label)) if (!v->text.empty()) out = v->text;
    };
    text("TXT_VICTORIOUS", a.title_win);
    text("TXT_SCENARIO_LOST", a.title_lose);
    text("GUI:SkirmishScore", a.heading);
    text("GUI:Time", a.time);
    text("GUI:Players", a.players);
    text("GUI:Kills", a.kills);
    text("GUI:Losses", a.losses);
    text("GUI:Built", a.built);
    text("GUI:Harvested", a.harvested);
    text("GUI:Continue", a.continue_text);
    return a;
}

std::string format_score_time(std::uint32_t s) {
    char b[32];
    std::snprintf(b, sizeof b, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

namespace {
constexpr gfx::Rgba kGold{240, 208, 96, 255}, kWhite{224, 224, 224, 255},
    kGreen{96, 224, 96, 255}, kRed{224, 80, 80, 255};

void blit(gfx::RgbaImage& dst, const gfx::RgbaImage& src, int ox, int oy) {
    for (int y = 0; y < src.height; ++y)
        for (int x = 0; x < src.width; ++x) {
            const int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height) continue;
            const auto s = (static_cast<std::size_t>(y) * src.width + x) * 4;
            if (!src.pixels[s + 3]) continue;
            const auto d = (static_cast<std::size_t>(dy) * dst.width + dx) * 4;
            std::copy_n(src.pixels.begin() + s, 4, dst.pixels.begin() + d);
        }
}
void fill(gfx::RgbaImage& img, int x0, int y0, int w, int h, gfx::Rgba c) {
    for (int y = std::max(0, y0); y < std::min<int>(img.height, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min<int>(img.width, x0 + w); ++x) {
            const auto i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            img.pixels[i] = c.r; img.pixels[i + 1] = c.g; img.pixels[i + 2] = c.b; img.pixels[i + 3] = 255;
        }
}
void text(gfx::RgbaImage& img, const GameFont& f, const std::string& s, int x, int y, gfx::Rgba c) {
    f.draw_text(img, s, x, y, c);
}
void text_right(gfx::RgbaImage& img, const GameFont& f, const std::string& s, int right, int y, gfx::Rgba c) {
    f.draw_text(img, s, right - static_cast<int>(f.text_width(s)), y, c);
}
void text_center(gfx::RgbaImage& img, const GameFont& f, const std::string& s, int cx, int y, gfx::Rgba c) {
    f.draw_text(img, s, cx - static_cast<int>(f.text_width(s)) / 2, y, c);
}
}

gfx::RgbaImage draw_score_screen(const ScoreAssets& a, const ScoreScreen& s) {
    if (s.houses.empty() || s.houses.size() > 8)
        throw std::invalid_argument("Score screen needs 1 to 8 houses");
    gfx::RgbaImage img;
    img.width = kScoreWidth; img.height = kScoreHeight;
    img.pixels.assign(std::size_t{kScoreWidth} * kScoreHeight * 4, 0);
    fill(img, 0, 0, kScoreWidth, kScoreHeight, {0, 0, 0, 255});
    // TODO-ORIGINAL dialog from gamemd.exe (score dialog resources): the
    // original places controls from a dialog template over the full screen.
    // This version centres the 640x400 original art and uses a fixed grid.
    const int ox = (kScoreWidth - 640) / 2, oy = (kScoreHeight - 400) / 2;
    if (a.background.width) blit(img, a.background, ox, oy);
    const GameFont& f = a.font;
    const int lh = static_cast<int>(f.line_height()) + 4;
    const int cx = kScoreWidth / 2;
    int y = oy + 12;
    text_center(img, f, a.heading, cx, y, kGold);
    y += lh + 4;
    text_center(img, f, s.player_won ? a.title_win : a.title_lose, cx, y,
                s.player_won ? kGreen : kRed);
    y += lh + 4;
    text_center(img, f, a.time + ": " + format_score_time(s.seconds), cx, y, kWhite);
    y += lh * 2;

    // Main table: player, kills, losses, built (units/buildings), harvested.
    const int left = ox + 30;
    const int c_kills = ox + 270, c_loss = ox + 350, c_built = ox + 470, c_harv = ox + 610;
    text(img, f, a.players, left, y, kGold);
    text_right(img, f, a.kills, c_kills, y, kGold);
    text_right(img, f, a.losses, c_loss, y, kGold);
    text_right(img, f, a.built, c_built, y, kGold);
    text_right(img, f, a.harvested, c_harv, y, kGold);
    y += lh;
    fill(img, left, y - 2, 580, 1, kGold);
    for (const auto& h : s.houses) {
        text(img, f, h.name, left, y, h.color);
        text_right(img, f, std::to_string(h.kills()), c_kills, y, kWhite);
        text_right(img, f, std::to_string(h.losses()), c_loss, y, kWhite);
        text_right(img, f, std::to_string(h.units_built) + "/" + std::to_string(h.buildings_built),
                   c_built, y, kWhite);
        text_right(img, f, std::to_string(h.credits_harvested), c_harv, y, kWhite);
        y += lh;
    }
    // Kill matrix: row = killer, column = victim.
    y += lh;
    text(img, f, a.kills, left, y, kGold);
    const int mx = ox + 160, step = 60;
    for (std::size_t v = 0; v < s.houses.size(); ++v)
        text_right(img, f, s.houses[v].name.substr(0, 6), mx + int(v + 1) * step, y, s.houses[v].color);
    y += lh;
    for (const auto& h : s.houses) {
        text(img, f, h.name, left, y, h.color);
        for (std::size_t v = 0; v < s.houses.size(); ++v) {
            const auto n = v < h.kills_of.size() ? h.kills_of[v] : 0;
            text_right(img, f, std::to_string(n), mx + int(v + 1) * step, y, kWhite);
        }
        y += lh;
    }
    text_center(img, f, a.continue_text, cx, oy + 400 - lh - 8, kGold);
    return img;
}
}
