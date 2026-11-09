#include "campaign_flow.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Two tiny [Basic] map documents in the shipped key layout.
void require(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}
using namespace ra2;

const char* allied_map =
    "[Basic]\n"
    "Briefing=Brief:ALL01\n"
    "Intro=ally_01\n"
    "Brief=alibrief\n"
    "Win=<none>\n"
    "Lose=<none>\n"
    "Action=<none>\n"
    "PostScore=<none>\n"
    "NextScenario=ALL02S.MAP\n"
    "AltNextScenario=ALL09T.MAP\n"
    "EndOfGame=no\n"
    "Official=yes\n";

const char* soviet_map =
    "[Basic]\n"
    "Briefing=Brief:SOV01\n"
    "Intro=<none>\n"
    "EndOfGame=yes\n"
    "Official=yes\n";

void push_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
}

// One label with one string value: " LBL", count, name length, name, " RTS", units.
void push_label(std::vector<std::uint8_t>& out, const std::string& name,
                const std::string& text) {
    out.insert(out.end(), {' ', 'L', 'B', 'L'});
    push_u32(out, 1);
    push_u32(out, static_cast<std::uint32_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), {' ', 'R', 'T', 'S'});
    push_u32(out, static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        out.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(c) ^ 0xff));
        out.push_back(0xff);
    }
}

// Builds a valid CSF string table with one label per campaign.
ra2text::CsfFile test_strings() {
    std::vector<std::uint8_t> blob;
    const std::uint32_t header[]{3, 5, 5, 0, 0}; // version, labels, strings, 0, 0
    blob.insert(blob.end(), {' ', 'F', 'S', 'C'});
    for (const std::uint32_t word : header)
        for (int i = 0; i < 4; ++i)
            blob.push_back(static_cast<std::uint8_t>((word >> (8 * i)) & 0xff));
    push_label(blob, "DESC:ALL1", "Allied campaign");
    push_label(blob, "DESC:SOV1", "Soviet campaign");
    push_label(blob, "Brief:ALL01", "The Soviets have invaded.");
    push_label(blob, "Name:ALL01", "Alpha");
    push_label(blob, "Brief:SOV01", "Comrade, the tide turns.");
    ra2text::CsfFile strings;
    strings.parse(blob.data(), blob.size());
    return strings;
}
} // namespace


int main() {
    const auto strings = test_strings();
    rules::IniFile battle;
    battle.parse("[Battles]\n1=ALL1\n2=SOV1\n3=GONE\n\n[ALL1]\nCD=0\nScenario=ALL01T.MAP\n"
                 "Description=DESC:ALL1\n\n[SOV1]\nCD=1\nScenario=SOV01T.MAP\n"
                 "FinalMovie=sovfin\nDescription=DESC:SOV1\nDebugOnly=yes\n");
    rules::IniFile missions;
    missions.parse("[SOV01T.MAP]\nBriefing=Brief:SOV01\n[ALL01T.MAP]\nBriefing=Brief:ALL01\n"
                   "UIName=Name:ALL01\n[ALL02S.MAP]\nBriefing=Brief:ALL02\n"
                   "[C1M1A.MAP]\nBriefing=COOP:x\n");
    const auto campaigns = ui::load_campaigns(battle, strings);
    require(campaigns.size() == 2, "campaign count skips missing section");
    require(campaigns[0].side == ui::CampaignSide::Allied, "allied side");
    require(campaigns[1].side == ui::CampaignSide::Soviet && campaigns[1].debug_only, "soviet side");
    require(campaigns[0].description == "Allied campaign", "description text");
    require(campaigns[1].final_movie == "sovfin" && campaigns[1].cd == 1, "final movie and cd");

    const auto allied = ui::list_missions(missions, strings, ui::CampaignSide::Allied);
    require(allied.size() == 2 && allied[0].scenario == "ALL01T.MAP" && allied[0].number == 1, "allied list");
    require(allied[0].briefing == "The Soviets have invaded." && allied[0].name == "Alpha", "listed text");
    const auto soviet = ui::list_missions(missions, strings, ui::CampaignSide::Soviet);
    require(soviet.size() == 1 && soviet[0].briefing == "Comrade, the tide turns.", "soviet list");

    auto reader = [](std::string_view name) {
        auto wrap = [](const char* s) {
            return std::optional<std::vector<std::uint8_t>>(
                std::vector<std::uint8_t>(s, s + std::strlen(s)));
        };
        if (name == "ALL01T.MAP") return wrap(allied_map);
        if (name == "ALL02S.MAP") return wrap(soviet_map);
        return std::optional<std::vector<std::uint8_t>>();
    };
    ui::CampaignFlow flow(campaigns, missions, strings, reader);
    require(!flow.select("nope"), "unknown campaign");
    require(flow.select("all1"), "select case-insensitive");
    const auto& m = flow.load_mission();
    require(m.briefing == "The Soviets have invaded.", "briefing via MISSION.INI");
    const auto movies = ui::movies_before(m);
    require(movies.size() == 2 && movies[0] == "ally_01" && movies[1] == "alibrief", "movies before");
    require(flow.next_scenario() == "ALL02S.MAP", "next from map");
    require(flow.advance_on_win() && flow.mission().scenario == "ALL02S.MAP", "advance");
    require(flow.mission().end_of_game && !flow.finished(), "last map flag");
    require(!flow.advance_on_win() && flow.finished(), "campaign ends");

    // Real game data when present: lists, briefings from the shipped CSF.
    const char* env = std::getenv("RA2_GAME_DIR");
    const std::filesystem::path dir = env ? env : "/path/to/user/ra2-test-host/game";
    if (std::filesystem::exists(dir / "ra2.mix")) {
        fs::MixFileSystem files(dir);
        files.mount_language(fs::GameVariant::RedAlert2);
        files.bootstrap(fs::GameVariant::RedAlert2);
        auto csf = files.read_file("RA2.CSF");
        auto bat = files.read_file("BATTLE.INI");
        auto mis = files.read_file("MISSION.INI");
        require(csf && bat && mis, "real files");
        ra2text::CsfFile real;
        real.parse(csf->data(), csf->size());
        rules::IniFile b, mi;
        b.parse(std::string_view(reinterpret_cast<const char*>(bat->data()), bat->size()));
        mi.parse(std::string_view(reinterpret_cast<const char*>(mis->data()), mis->size()));
        const auto rc = ui::load_campaigns(b, real);
        require(rc.size() >= 3 && rc[1].id == "ALL1" && rc[2].id == "SOV1", "real campaigns");
        require(!rc[1].description.empty() && rc[1].description != "DESC:ALL1", "real description text");
        for (auto side : {ui::CampaignSide::Allied, ui::CampaignSide::Soviet}) {
            const auto list = ui::list_missions(mi, real, side);
            require(list.size() == 12, "twelve missions per side");
            for (const auto& item : list) {
                require(item.briefing.size() > 20 && item.briefing.find("Brief:") != 0,
                        "real briefing text");
            }
        }
        std::cout << "Real data: " << rc.size() << " campaigns, 12+12 missions\n";
    } else {
        std::cout << "Real game data not found; skipped\n";
    }
    std::cout << "Campaign flow tests passed\n";
    return 0;
}
