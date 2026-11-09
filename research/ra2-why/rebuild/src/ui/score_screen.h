#pragma once
// End-of-match score screen for the playable native prototype.
// Draws a CPU RGBA image: original mpscore.pcx art, GAME.FNT font and CSF
// text. Layout is a simple version; see TODO-ORIGINAL in score_screen.cpp.
#include "ui.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ra2::ui {
// Totals for one house. kills_of[v] counts objects of house v that this
// house destroyed. losses_to[k] counts own objects that house k destroyed.
struct ScoreHouse {
    std::string name;
    gfx::Rgba color{224, 224, 224, 255};
    std::uint32_t units_built = 0, buildings_built = 0;
    std::uint32_t credits_harvested = 0;
    std::vector<std::uint32_t> kills_of, losses_to; // Indexed by house row.
    std::uint32_t kills() const noexcept;
    std::uint32_t losses() const noexcept;
};

struct ScoreScreen {
    bool player_won = false;
    std::uint32_t seconds = 0;       // Time played.
    std::vector<ScoreHouse> houses;  // houses[0] is the local player.
};

// Collects events during a match. Feed it from the sim, then call finish().
class ScoreTally {
public:
    explicit ScoreTally(std::vector<std::string> names);
    void built(std::size_t house, bool is_building);
    void harvested(std::size_t house, std::uint32_t credits);
    // killer may be npos for a death with no killer (no one gets a kill).
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    void destroyed(std::size_t killer, std::size_t victim);
    ScoreScreen finish(bool player_won, std::uint32_t seconds) const;
private:
    ScoreScreen state_;
};

// Text and art for the screen. Defaults are the English original strings.
struct ScoreAssets {
    GameFont font;
    gfx::RgbaImage background; // Empty means plain black.
    std::string title_win = "You Are Victorious!", title_lose = "Mission Failed";
    std::string heading = "Skirmish Score", time = "Time", players = "Players";
    std::string kills = "Kills", losses = "Losses", built = "Built";
    std::string harvested = "Harvested", continue_text = "Continue";
    // Reads GAME.FNT, mpscore.pcx and CSF labels. Missing art or labels
    // fall back to the defaults. Throws only if GAME.FNT is missing.
    static ScoreAssets load(const fs::MixFileSystem& files, const ra2text::CsfFile& strings);
};

// "H:MM:SS" clock text; hours are not clipped.
std::string format_score_time(std::uint32_t seconds);

constexpr unsigned kScoreWidth = 800, kScoreHeight = 600;
// Returns a new 800x600 image. Throws std::invalid_argument if there are no
// houses or more than 8.
gfx::RgbaImage draw_score_screen(const ScoreAssets& assets, const ScoreScreen& screen);
}
