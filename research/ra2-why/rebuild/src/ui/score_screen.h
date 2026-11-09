#pragma once
// Original-style end-of-match score screen. Draws win or lose, the match
// length, and the per-house production, kill and loss table into a caller
// supplied RGBA image. The original gamemd.exe score screen 0x0067E7C0
// (ScoreScreenClass) keeps its layout in dialog resources; this rebuild
// draws the same columns with the GAME.FNT bitmap font instead. The
// TODO-ORIGINAL markers below list the parts not yet verified against the
// original layout code.
#include "ui.h"
#include "../gfx/shp_decode.h"
#include <string>
#include <vector>

namespace ra2::ui {
// One house in the end-of-match table. Built counts production; lost and
// killed count combat deaths. The original accumulates these in the house
// statistics at HouseClass +0x4D8 (units built), +0x4DC (buildings built)
// and the damage/kill tallies the score screen reads at 0x0067E7C0.
struct ScoreSide {
    std::string name;          // House label shown in the row.
    std::uint32_t units_built = 0;
    std::uint32_t buildings_built = 0;
    std::uint32_t units_lost = 0;
    std::uint32_t units_killed = 0;
    std::uint32_t credits_harvested = 0;
};

// End-of-match data. rows[0] is the local player; the rest follow in table
// order. player_won picks the title: Mission Accomplished or Mission Failed.
struct ScoreScreen {
    bool player_won = false;
    std::uint32_t seconds = 0; // Match length in seconds.
    std::vector<ScoreSide> rows;
};

// Formats seconds as the original H:MM:SS clock (hours are not clipped).
std::string format_score_time(std::uint32_t seconds);

// Draws the whole score screen into target at the original 800x600 layout.
// Throws std::runtime_error when target is not 800x600 or any row is empty.
// The panel covers the image, so a caller may reuse a fresh or dirty buffer.
void draw_score_screen(gfx::RgbaImage& target, const ScoreScreen& screen,
                       const GameFont& font);
} // namespace ra2::ui
