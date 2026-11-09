#include "campaign_flow.h"

#include "../rules/ini_file.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace ra2::ui {
namespace {
std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

// CSF labels are looked up in upper case, like the original.
std::string resolve(const ra2text::CsfFile& strings, std::string_view label) {
    if (label.empty()) return {};
    const std::string key(label);
    if (const auto* v = strings.find(key)) return v->text;
    if (const auto* v = strings.find(upper(label))) return v->text;
    // Labels in the shipped table are mixed case; scan once as a fallback.
    const std::string want = upper(label);
    for (const auto& l : strings.labels())
        if (upper(l.name) == want && !l.values.empty()) return l.values[0].text;
    return key;
}

bool is_none(std::string_view s) {
    return s.empty() || upper(s) == "<NONE>";
}

// "ALL01T.MAP" -> side Allied, number 1. Other names give Other, 0.
void classify(const std::string& map, CampaignSide& side, int& number) {
    side = CampaignSide::Other;
    number = 0;
    const std::string n = upper(map);
    if (n.size() < 5) return;
    const std::string p = n.substr(0, 3);
    if (p == "ALL") side = CampaignSide::Allied;
    else if (p == "SOV") side = CampaignSide::Soviet;
    else if (p == "TRN") side = CampaignSide::Training;
    else return;
    if (std::isdigit(static_cast<unsigned char>(n[3])) && std::isdigit(static_cast<unsigned char>(n[4])))
        number = (n[3] - '0') * 10 + (n[4] - '0');
}

CampaignSide side_of_campaign(const std::string& id, const std::string& scenario) {
    CampaignSide side; int number;
    classify(scenario, side, number);
    if (side != CampaignSide::Other) return side;
    classify(id + "00", side, number);
    return side;
}
} // namespace

std::vector<std::string> movies_before(const CampaignMission& m) {
    std::vector<std::string> out;
    for (const auto* name : {&m.intro_movie, &m.brief_movie, &m.action_movie})
        if (!is_none(*name)) out.push_back(*name);
    return out;
}

std::vector<CampaignEntry> load_campaigns(const rules::IniFile& battle_ini,
                                          const ra2text::CsfFile& strings) {
    const auto* list = battle_ini.find_section("Battles");
    if (!list) throw std::runtime_error("Missing BATTLE.INI [Battles]");
    std::vector<CampaignEntry> result;
    for (const auto& row : list->entries) {
        if (!battle_ini.find_section(row.value)) continue;
        CampaignEntry c;
        c.id = row.value;
        c.scenario = battle_ini.read_string(row.value, "Scenario");
        if (c.scenario.empty()) continue;
        c.index = static_cast<int>(result.size()) + 1;
        c.side = side_of_campaign(c.id, c.scenario);
        c.description = resolve(strings, battle_ini.read_string(row.value, "Description"));
        c.final_movie = battle_ini.read_string(row.value, "FinalMovie");
        c.cd = battle_ini.read_integer(row.value, "CD", 0);
        c.debug_only = battle_ini.read_bool(row.value, "DebugOnly", false);
        result.push_back(std::move(c));
    }
    return result;
}

std::vector<MissionListing> list_missions(const rules::IniFile& mission_ini,
                                          const ra2text::CsfFile& strings,
                                          CampaignSide side) {
    std::vector<MissionListing> out;
    for (const auto& section : mission_ini.sections()) {
        MissionListing m;
        classify(section.name, m.side, m.number);
        if (m.side != side || m.number == 0) continue;
        m.scenario = section.name;
        m.name = resolve(strings, mission_ini.read_string(section.name, "UIName"));
        m.briefing = resolve(strings, mission_ini.read_string(section.name, "Briefing"));
        out.push_back(std::move(m));
    }
    return out;
}

CampaignMission parse_campaign_mission(const std::string& battle_id,
    const std::string& scenario, std::string_view map_text,
    const rules::IniFile& mission_ini, const ra2text::CsfFile& strings) {
    rules::IniFile map;
    map.parse(map_text);
    if (!map.find_section("Basic")) throw std::runtime_error("Mission map has no [Basic]: " + scenario);
    CampaignMission m;
    m.battle_id = battle_id;
    m.scenario = scenario;
    classify(scenario, m.side, m.number);
    // The briefing label lives in MISSION.INI under the map file name; the
    // map [Basic] Briefing is only a fallback. TODO-ORIGINAL: original reads
    // MISSION.INI in the mission start code (game.exe Scenario load).
    std::string section = upper(scenario);
    std::string label = mission_ini.read_string(section, "Briefing");
    if (label.empty()) label = map.read_string("Basic", "Briefing");
    m.briefing = resolve(strings, label);
    m.name = resolve(strings, mission_ini.read_string(section, "UIName"));
    if (m.name.empty()) m.name = resolve(strings, map.read_string("Basic", "Name"));
    m.intro_movie = map.read_string("Basic", "Intro");
    m.brief_movie = map.read_string("Basic", "Brief");
    m.action_movie = map.read_string("Basic", "Action");
    m.win_movie = map.read_string("Basic", "Win");
    m.lose_movie = map.read_string("Basic", "Lose");
    m.post_score = map.read_string("Basic", "PostScore");
    m.next_scenario = map.read_string("Basic", "NextScenario");
    m.alt_next_scenario = map.read_string("Basic", "AltNextScenario");
    m.end_of_game = map.read_bool("Basic", "EndOfGame", false);
    m.map_text = std::string(map_text);
    return m;
}

CampaignFlow::CampaignFlow(std::vector<CampaignEntry> campaigns,
                           rules::IniFile mission_ini, ra2text::CsfFile strings,
                           CampaignFileReader read_map)
    : campaigns_(std::move(campaigns)),
      mission_ini_(std::make_unique<rules::IniFile>(std::move(mission_ini))),
      strings_(std::move(strings)), read_map_(std::move(read_map)) {}
CampaignFlow::~CampaignFlow() = default;
CampaignFlow::CampaignFlow(CampaignFlow&&) noexcept = default;
CampaignFlow& CampaignFlow::operator=(CampaignFlow&&) noexcept = default;

bool CampaignFlow::select(const std::string& id) {
    const std::string want = upper(id);
    for (std::size_t i = 0; i < campaigns_.size(); ++i) {
        if (upper(campaigns_[i].id) == want) {
            selected_ = i;
            mission_.reset();
            finished_ = false;
            return true;
        }
    }
    return false;
}

CampaignMission CampaignFlow::load_map(const std::string& scenario) const {
    const auto data = read_map_ ? read_map_(scenario) : std::nullopt;
    if (!data) throw std::runtime_error("Missing campaign map: " + scenario);
    return parse_campaign_mission(campaigns_[*selected_].id, scenario,
        std::string_view(reinterpret_cast<const char*>(data->data()), data->size()),
        *mission_ini_, strings_);
}

const CampaignMission& CampaignFlow::load_mission() {
    if (!selected_) throw std::runtime_error("No campaign selected");
    mission_ = load_map(campaigns_[*selected_].scenario);
    finished_ = false;
    return *mission_;
}

const CampaignMission& CampaignFlow::mission() const {
    if (!mission_) throw std::runtime_error("No mission loaded");
    return *mission_;
}

std::string CampaignFlow::next_scenario() const {
    if (!mission_) throw std::runtime_error("No mission loaded");
    if (mission_->end_of_game) return {};
    if (!is_none(mission_->next_scenario)) return mission_->next_scenario;
    // TODO-ORIGINAL: the original reads only the map NextScenario. When a map
    // gives none (maps missing from the local files), step to the next
    // MISSION.INI entry of the same side so the campaign stays playable.
    if (mission_->number == 0) return {};
    bool seen = false;
    for (const auto& s : mission_ini_->sections()) {
        CampaignSide side; int number;
        classify(s.name, side, number);
        if (side != mission_->side || number == 0) continue;
        if (seen) return s.name;
        if (upper(s.name) == upper(mission_->scenario)) seen = true;
    }
    return {};
}

bool CampaignFlow::advance_on_win() {
    const std::string next = next_scenario();
    if (next.empty()) {
        finished_ = true;
        return false;
    }
    mission_ = load_map(next);
    return true;
}

} // namespace ra2::ui
