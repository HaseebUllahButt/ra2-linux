#include "campaign_flow.h"

#include "../rules/ini_file.h"
#include <stdexcept>
#include <utility>

namespace ra2::ui {
namespace {
// CSF label lookup. The original game uppercases the label before the
// search, so mixed-case spellings resolve to the same string.
const ra2text::CsfValue* find_string(const ra2text::CsfFile& strings,
                                     const std::string& label) {
    if (label.empty()) return nullptr;
    if (const auto* exact = strings.find(label)) return exact;
    std::string upper(label);
    for (char& c : upper)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return strings.find(upper);
}

// Resolves one CSF label. Unresolved labels keep the raw value.
std::string resolve(const ra2text::CsfFile& strings, std::string_view label) {
    if (label.empty()) return {};
    const auto* text = find_string(strings, std::string(label));
    return text ? text->text : std::string(label);
}
} // namespace

std::vector<CampaignEntry> load_campaigns(const rules::IniFile& battle_ini,
                                          const ra2text::CsfFile& strings) {
    const auto* list = battle_ini.find_section("Battles");
    if (!list) throw std::runtime_error("Missing original Battles list");
    std::vector<CampaignEntry> result;
    for (const auto& row : list->entries) {
        // The original skips rows without a battle section; the shipped
        // battle.ini lists debug-only rows the same way.
        const auto* data = battle_ini.find_section(row.value);
        if (!data) continue;
        CampaignEntry campaign;
        campaign.index = static_cast<int>(result.size()) + 1;
        campaign.id = row.value;
        campaign.scenario = battle_ini.read_string(row.value, "Scenario");
        campaign.description = resolve(strings, battle_ini.read_string(row.value, "Description"));
        campaign.final_movie = resolve(strings, battle_ini.read_string(row.value, "FinalMovie"));
        campaign.cd = battle_ini.read_integer(row.value, "CD", 0);
        campaign.debug_only = battle_ini.read_bool(row.value, "DebugOnly", false);
        result.push_back(std::move(campaign));
    }
    return result;
}

CampaignMission parse_campaign_mission(const std::string& battle_id,
    const std::string& scenario, std::string_view map_text,
    const ra2text::CsfFile& strings) {
    rules::IniFile map;
    map.parse(map_text);
    CampaignMission mission;
    mission.battle_id = battle_id;
    mission.scenario = scenario;
    mission.briefing = resolve(strings, map.read_string("Basic", "Briefing"));
    mission.intro_movie = map.read_string("Basic", "Intro");
    mission.brief_movie = map.read_string("Basic", "Brief");
    mission.win_movie = map.read_string("Basic", "Win");
    mission.lose_movie = map.read_string("Basic", "Lose");
    mission.post_score = map.read_string("Basic", "PostScore");
    mission.next_scenario = map.read_string("Basic", "NextScenario");
    mission.alt_next_scenario = map.read_string("Basic", "AltNextScenario");
    mission.official = rules::IniFile::bool_value(
                           map.read_string("Basic", "Official"))
                           .value_or(false);
    mission.end_of_game = rules::IniFile::bool_value(
                              map.read_string("Basic", "EndOfGame"))
                              .value_or(false);
    return mission;
}

// TODO-ORIGINAL: the original keeps the win/lose flag in the mission object
// and asks the player on the score screen; the flow here only records it.
CampaignFlow::CampaignFlow(std::vector<CampaignEntry> campaigns,
                           ra2text::CsfFile strings, CampaignFileReader read_map)
    : campaigns_(std::move(campaigns)), strings_(std::move(strings)),
      read_map_(std::move(read_map)) {}

bool CampaignFlow::select(const std::string& id) noexcept {
    for (std::size_t i = 0; i < campaigns_.size(); ++i) {
        if (campaigns_[i].id == id) {
            selected_ = i;
            mission_.reset();
            finished_ = false;
            return true;
        }
    }
    return false;
}

const CampaignMission& CampaignFlow::load_mission() {
    if (!selected_) throw std::runtime_error("No campaign selected");
    const auto& campaign = campaigns_[*selected_];
    const auto data = read_map_ ? read_map_(campaign.scenario) : std::nullopt;
    if (!data) throw std::runtime_error("Missing campaign map: " + campaign.scenario);
    mission_ = parse_campaign_mission(campaign.id, campaign.scenario,
        std::string_view(reinterpret_cast<const char*>(data->data()), data->size()),
        strings_);
    return *mission_;
}

bool CampaignFlow::advance_on_win() {
    if (!mission_) throw std::runtime_error("No mission loaded");
    // The original follows NextScenario after a win; AltNextScenario is the
    // branch fallback. An empty name ends the campaign.
    const auto& next = !mission_->next_scenario.empty()
                           ? mission_->next_scenario
                           : mission_->alt_next_scenario;
    if (next.empty() || !read_map_) {
        finished_ = true;
        return false;
    }
    const auto data = read_map_(next);
    if (!data) throw std::runtime_error("Missing campaign map: " + next);
    mission_ = parse_campaign_mission(mission_->battle_id, next,
        std::string_view(reinterpret_cast<const char*>(data->data()), data->size()),
        strings_);
    return true;
}

} // namespace ra2::ui
