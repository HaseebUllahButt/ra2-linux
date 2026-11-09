#pragma once
// Campaign flow: lists the Allied and Soviet missions, loads one mission map
// with its briefing text and movies, and finds the next mission after a win.
// Data: BATTLE.INI (campaign list), MISSION.INI (briefing label per map),
// the mission map [Basic] section (movies, next map) and the CSF string table.

#include "../text/csf.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ra2::rules { class IniFile; }

namespace ra2::ui {

enum class CampaignSide { Allied, Soviet, Training, Other };

// One row of the BATTLE.INI [Battles] list.
struct CampaignEntry {
    int index = 0;               // 1-based position in [Battles].
    std::string id;              // Section name, e.g. "ALL1".
    CampaignSide side = CampaignSide::Other;
    std::string scenario;        // First mission map file.
    std::string description;     // Description text from the CSF table.
    std::string final_movie;     // Movie after the last win.
    int cd = 0;                  // Required CD, -1 means any CD.
    bool debug_only = false;     // Hidden in the shipped menu.
};

// One mission from MISSION.INI, in file order (ALL01T.MAP ... SOV12S.MAP).
struct MissionListing {
    std::string scenario;        // Map file name, e.g. "ALL01T.MAP".
    CampaignSide side = CampaignSide::Other;
    int number = 0;              // Mission number, 1-based (ALL01 = 1).
    std::string name;            // UIName text.
    std::string briefing;        // Briefing text.
};

// One loaded mission map with its briefing and movies.
struct CampaignMission {
    std::string battle_id;       // Owning campaign id, e.g. "ALL1".
    std::string scenario;        // Map file name.
    CampaignSide side = CampaignSide::Other;
    int number = 0;
    std::string name;            // UIName text.
    std::string briefing;        // Briefing text.
    std::string intro_movie;     // Played before the briefing.
    std::string brief_movie;     // Played during the briefing.
    std::string action_movie;    // Played when the mission starts.
    std::string win_movie;
    std::string lose_movie;
    std::string post_score;      // Played after the score screen.
    std::string next_scenario;   // From the map; empty when the map has none.
    std::string alt_next_scenario;
    bool end_of_game = false;
    std::string map_text;        // Raw map document for the world loader.
};

// Movie names to play before a mission, in order. Empty and "<none>" names
// are dropped.
std::vector<std::string> movies_before(const CampaignMission& mission);

using CampaignFileReader =
    std::function<std::optional<std::vector<std::uint8_t>>(std::string_view)>;

std::vector<CampaignEntry> load_campaigns(const rules::IniFile& battle_ini,
                                          const ra2text::CsfFile& strings);

// Allied and Soviet missions from MISSION.INI, in original order. Training
// and co-op maps are not listed.
std::vector<MissionListing> list_missions(const rules::IniFile& mission_ini,
                                          const ra2text::CsfFile& strings,
                                          CampaignSide side);

// Parses one mission map. Throws std::runtime_error without [Basic].
CampaignMission parse_campaign_mission(const std::string& battle_id,
    const std::string& scenario, std::string_view map_text,
    const rules::IniFile& mission_ini, const ra2text::CsfFile& strings);

class CampaignFlow {
public:
    CampaignFlow(std::vector<CampaignEntry> campaigns, rules::IniFile mission_ini,
                 ra2text::CsfFile strings, CampaignFileReader read_map);
    ~CampaignFlow();
    CampaignFlow(CampaignFlow&&) noexcept;
    CampaignFlow& operator=(CampaignFlow&&) noexcept;

    const std::vector<CampaignEntry>& campaigns() const noexcept { return campaigns_; }

    // Picks a campaign by id (case-insensitive). False when unknown.
    bool select(const std::string& id);

    // Loads the first map of the selected campaign. Throws std::runtime_error
    // when nothing is selected or the map is missing.
    const CampaignMission& load_mission();
    const CampaignMission& mission() const;

    // Name of the map that follows after a win; empty at the campaign end.
    // Uses the map NextScenario. TODO-ORIGINAL AltNextScenario branch
    // (original picks it by a mission flag the map script sets).
    std::string next_scenario() const;

    // After a win: loads the next map. False at the campaign end (finished()
    // becomes true and the current mission stays).
    bool advance_on_win();
    bool finished() const noexcept { return finished_; }

private:
    CampaignMission load_map(const std::string& scenario) const;
    std::vector<CampaignEntry> campaigns_;
    std::unique_ptr<rules::IniFile> mission_ini_;
    ra2text::CsfFile strings_;
    CampaignFileReader read_map_;
    std::optional<std::size_t> selected_;
    std::optional<CampaignMission> mission_;
    bool finished_ = false;
};

} // namespace ra2::ui
