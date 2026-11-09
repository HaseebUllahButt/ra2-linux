#include "campaign_flow.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Two tiny [Basic] map documents in the shipped key layout.
const char* allied_map =
    "[Basic]\n"
    "Briefing=Brief:ALL01\n"
    "Intro=A01_f00e\n"
    "Brief=<none>\n"
    "Win=<none>\n"
    "Lose=<none>\n"
    "Action=<none>\n"
    "PostScore=<none>\n"
    "NextScenario=GDI2A.map\n"
    "AltNextScenario=GDI9C.MAP\n"
    "EndOfGame=no\n"
    "Official=yes\n";

const char* soviet_map =
    "[Basic]\n"
    "Briefing=Brief:SOV01\n"
    "Intro=<none>\n"
    "NextScenario=\n"
    "EndOfGame=yes\n"
    "Official=yes\n";

// Appends one CSF string value: " RTS" marker, unit count, XOR 0xFF UTF-16.
void push_string(std::vector<std::uint8_t>& out, const std::string& text) {
    const std::uint32_t units = static_cast<std::uint32_t>(text.size());
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((units >> (8 * i)) & 0xff));
    for (const char c : text) {
        const auto unit = static_cast<std::uint8_t>(c);
        out.push_back(static_cast<std::uint8_t>(unit ^ 0xff));
        out.push_back(0xff);
    }
}

void push_label(std::vector<std::uint8_t>& out, const std::string& name,
                const std::string& text) {
    // " LBL" + one value + the label name bytes.
    const std::uint32_t one = 1;
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((one >> (8 * i)) & 0xff));
    const auto length = static_cast<std::int32_t>(name.size());
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((length >> (8 * i)) & 0xff));
    out.insert(out.end(), name.begin(), name.end());
    push_string(out, text);
}

// Builds a valid CSF string table with one label per campaign.
ra2text::CsfFile test_strings() {
    std::vector<std::uint8_t> blob;
    const std::uint32_t header[]{3, 2, 2, 0, 0}; // version, labels, strings, 0, 0
    blob.insert(blob.end(), {' ', 'F', 'S', 'C'});
    for (const std::uint32_t word : header)
        for (int i = 0; i < 4; ++i)
            blob.push_back(static_cast<std::uint8_t>((word >> (8 * i)) & 0xff));
    push_label(blob, "DESC:ALL1", "Allied campaign");
    push_label(blob, "DESC:SOV1", "Soviet campaign");
    push_label(blob, "Brief:ALL01", "The Soviets have invaded.");
    push_label(blob, "Brief:SOV01", "Comrade, the tide turns.");
    ra2text::CsfFile strings;
    strings.parse(blob.data(), blob.size());
    return strings;
}
} // namespace

int main() {
    const auto strings = test_strings();
    const auto* resolved = strings.find("DESC:ALL1");
    require(resolved && resolved->text == "Allied campaign", "test string table");

    // The shipped battle.ini rows: two campaigns, one per side.
    const char* battle_ini =
        "[Battles]\n"
        "1=ALL1\n"
        "2=SOV1\n"
        "\n"
        "[ALL1]\n"
        "CD=0\n"
        "Scenario=ALL01T.MAP\n"
        "FinalMovie=\n"
        "Description=DESC:ALL1\n"
        "\n"
        "[SOV1]\n"
        "CD=1\n"
        "Scenario=SOV01T.MAP\n"
        "FinalMovie=SOVFIN.BIK\n"
        "Description=DESC:SOV1\n";

    rules::IniFile ini;
    ini.parse(battle_ini);
    const auto campaigns = ra2::ui::load_campaigns(ini, strings);
    require(campaigns.size() == 2, "campaign count");
    require(campaigns[0].id == "ALL1" && campaigns[0].index == 1, "first campaign id");
    require(campaigns[0].scenario == "ALL01T.MAP", "first campaign scenario");
    require(campaigns[0].description == "Allied campaign", "first campaign description");
    require(campaigns[1].id == "SOV1" && campaigns[1].index == 2, "second campaign id");
    require(campaigns[1].cd == 1, "second campaign CD");
    require(campaigns[1].final_movie == "SOVFIN.BIK", "second campaign final movie");

    ra2::ui::CampaignFlow flow(campaigns, strings, [](std::string_view name) {
        if (name == "ALL01T.MAP") return std::optional<std::vector<std::uint8_t>>(
            std::vector<std::uint8_t>(allied_map, allied_map + std::strlen(allied_map)));
        if (name == "SOV01T.MAP") return std::optional<std::vector<std::uint8_t>>(
            std::vector<std::uint8_t>(soviet_map, soviet_map + std::strlen(soviet_map)));
        return std::optional<std::vector<std::uint8_t>>();
    });
    require(flow.select("ALL1"), "select ALL1");
    const auto& mission = flow.load_mission();
    require(mission.briefing == "The Soviets have invaded.", "allied briefing");
    require(mission.intro_movie == "A01_f00e", "allied intro movie");
    require(mission.next_scenario == "GDI2A.map", "allied next scenario");
    require(flow.advance_on_win(), "advance to next mission");
    require(flow.mission().scenario == "GDI2A.map", "advanced scenario");
    require(!flow.finished(), "campaign not finished mid run");
    require(flow.advance_on_win(), "advance to last mission");
    require(flow.mission().scenario == "SOV01T.MAP", "last mission scenario");
    require(!flow.advance_on_win(), "campaign ends after last mission");
    require(flow.finished(), "campaign finished");

    std::cout << "Campaign flow tests passed\n";
    return 0;
}
