#pragma once
// Original campaign flow: the battle.ini campaign list, one loaded mission
// map, and the step to the next mission after a win.

#include "../../text/csf.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ra2::rules { class IniFile; }

namespace ra2::ui {

// One row of the original [Battles] list with its resolved menu text.
struct CampaignEntry {
    int index = 0;               // Original 1-based [Battles] position.
    std::string id;              // Section name, e.g. "ALL1".
    std::string scenario;        // First mission map file.
    std::string description;     // Resolved Description text.
    std::string final_movie;     // Finale movie after the last win.
    int cd = 0;                  // Required CD, -1 means any CD.
    bool debug_only = false;     // Original DebugOnly flag.
};

// One loaded mission: the map, its briefing text and the movies around it.
struct CampaignMission {
    std::string battle_id;       // Owning campaign id, e.g. "ALL1".
    std::string scenario;        // Map file name, e.g. "ALL01t.MAP".
    std::string briefing;        // Resolved Briefing text.
    std::string briefing_key;    // Raw Briefing value, e.g. "Brief:ALL01".
    std::string intro_movie;     // Basic Intro movie.
    std::string brief_movie;     // Basic Brief movie.
    std::string win_movie;       // Basic Win movie.
    std::string lose_movie;      // Basic Lose movie.
    std::string post_score;      // Basic PostScore movie.
    std::string next_scenario;   // Basic NextScenario.
    std::string alt_next_scenario; // Basic AltNextScenario.
    bool official = false;       // Basic Official flag.
    bool end_of_game = false;    // Basic EndOfGame flag.
};

// Game file lookup by name. MixFileSystem::read_file has this shape, so a
// mounted filesystem binds without a wrapper.
using CampaignFileReader =
    std::function<std::optional<std::vector<std::uint8_t>>(std::string_view)>;

// Ordered [Battles] rows of the given battle.ini document. Rows whose
// section or Scenario value is missing are skipped, like the original
// campaign dialog. Descriptions keep the raw value; no CSF is read here.
std::vector<CampaignEntry> load_campaigns(const rules::IniFile& battle_ini);

// Parses one mission map document. `battle_id` marks the owning campaign.
// The briefing text is the resolved [Basic] Briefing CSF label; the movie
// fields copy the [Basic] Intro, Brief, Win, Lose, Action and PostScore
// values. Throws std::runtime_error when the document has no [Basic] section.
CampaignMission parse_campaign_mission(const std::string& battle_id,
    const std::string& scenario, std::string_view map_text,
    const ra2text::CsfFile& strings);

// Drives one campaign: select, load, win, next mission.
class CampaignFlow {
public:
    CampaignFlow(std::vector<CampaignEntry> campaigns, ra2text::CsfFile strings,
                 CampaignFileReader read_map);

    const std::vector<CampaignEntry>& campaigns() const noexcept { return campaigns_; }

    // Chooses a campaign by id, case-insensitive like the original section
    // lookup. Returns false when no campaign carries that id.
    bool select(const std::string& id) noexcept;

    // Reads the selected campaign's current mission map and briefing.
    // Throws std::runtime_error when no campaign is selected or the map is
    // missing or has no [Basic] section.
    const CampaignMission& load_mission();

    // Loaded mission of the selected campaign. Throws std::runtime_error
    // before the first load_mission call.
    const CampaignMission& mission() const;

    // On a win, moves to the next mission of the selected campaign. Returns
    // false when the campaign has no next mission; the loaded mission stays.
    bool advance_on_win();

    // True after a win on a mission without a next scenario.
    bool finished() const noexcept { return finished_; }

private:
    std::vector<CampaignEntry> campaigns_;
    ra2text::CsfFile strings_;
    CampaignFileReader read_map_;
    std::optional<std::size_t> selected_;
    std::optional<CampaignMission> mission_;
    bool finished_ = false;
};

} // namespace ra2::ui
