#include "ini_file.h"
#include "rules_reader.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using ra2::rules::IniFile;
using ra2::rules::RulesReader;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Check failed: " + message);
}

void parser_checks() {
    IniFile ini;
    ini.parse("preamble=ignored\r\n; comment\r\n[Empty]\r\n"
              " [General] ; section comment\r\n RepairStep = 8 ; inline\r\n"
              "empty= ; no value\r\n=bad\r\ninvalid line\r\n"
              "hex=$FF\r\nsuffix=FFFFFFFFh\r\ndecimal=010\r\n"
              "prefix=12junk\r\nzero=garbage\r\nrate=15%\r\n"
              "flag=Yesterday\r\nno=Never\r\nunknown=maybe\r\n"
              "quoted=\"text;comment\"\r\nlist=A, B,, C\r\n"
              "equals=A=B\r\nhash=#literal\r\n"
              "RepairStep=99\r\nrepairStep=4\r\n"
              "[General]\r\nRepairStep=101\r\n[Other]\r\nx=last");
    require(ini.section_count() == 3, "empty sections discarded; repeated blocks preserved");
    require(!ini.find_section("general"), "case-sensitive sections");
    require(ini.read_integer("General", "RepairStep") == 8, "documented first duplicate policy");
    require(ini.read_integer("General", "repairStep") == 4, "case-sensitive keys");
    require(ini.statistics().duplicate_keys == 1 && ini.statistics().duplicate_sections == 1,
            "duplicate diagnostics");
    require(!ini.contains("General", "empty"), "empty values omitted");
    require(ini.read_integer("General", "hex") == 255, "dollar hex");
    require(ini.read_integer("General", "suffix") == -1, "hex suffix and 32-bit bits");
    require(ini.read_integer("General", "decimal") == 10, "decimal leading zero");
    require(ini.read_integer("General", "prefix") == 12, "atoi prefix");
    require(ini.read_integer("General", "zero", 17) == 0, "atoi invalid decimal");
    require(ini.read_double("General", "rate") == 0.15, "percent conversion");
    require(ini.read_bool("General", "flag"), "boolean first character");
    require(!ini.read_bool("General", "no", true), "negative boolean first character");
    require(ini.read_bool("General", "unknown", true), "invalid boolean fallback");
    require(ini.read_string("General", "quoted") == "\"text", "semicolon inside quotes");
    require(ini.read_string("General", "equals") == "A=B", "first equals only");
    require(ini.read_string("General", "hash") == "#literal", "hash retained");
    require(ini.read_list("General", "list") == std::vector<std::string>({"A", "B", "C"}),
            "comma list");
    require(ini.read_string("Other", "x") == "last", "EOF without newline");
    require(!ini.key_at("General", 10000), "enumeration bound");
    require(ini.key_at("General", 0)->key == "RepairStep", "entry order");
    std::istringstream broken("unreadable");
    broken.setstate(std::ios::badbit);
    bool failed = false;
    try { ini.parse(broken); } catch (const std::runtime_error&) { failed = true; }
    require(failed && ini.read_string("Other", "x") == "last", "transactional stream failure");
    ini.parse("[New]\nvalue=" + std::string(2000, 'x') + "\n");
    require(ini.read_string("New", "value").size() == 2000, "safe unbounded line extension");
    require(!ini.find_section("General"), "parse replaces prior document");
}

void rules_checks() {
    RulesReader rules;
    require(rules.get<std::int32_t>("SelfHealUnitFrames") == 1000, "constructor integer");
    require(rules.get<double>("RepairPercent") == 0.25, "constructor double");
    require(!rules.get<std::int32_t>("AICaptureLowMoneyMark"), "unknown constructor default explicit");
    IniFile first;
    first.parse("[General]\nBuildSpeed=.7\nRepairStep=8\nTeamDelays=1,, -2,garbage,0x10\n"
                "ChronoTrigger=no\nCliffBackImpassability=257\nDMislAcceleration=.25\n"
                "DeferredType=MTNK\n");
    auto report = rules.init(first);
    require(report.section_present && report.loaded == 6, "supported General fields loaded");
    require(report.unhandled_keys == std::vector<std::string>{"DeferredType"}, "deferred field reported");
    require(rules.get<double>("BuildSpeed") == static_cast<double>(0.7f), "ReadDouble float precision");
    require(rules.get<std::int32_t>("RepairStep") == 8, "integer rule");
    require(rules.get<bool>("ChronoTrigger") == false, "boolean rule");
    require(rules.get<std::int32_t>("CliffBackImpassability") == 1, "byte narrowing");
    require(rules.get<std::vector<std::int32_t>>("TeamDelays") ==
                std::vector<std::int32_t>({1, -2, 0, 0}), "integer lists use decimal atoi");
    require(rules.get<double>("CMislAcceleration") == 0.25, "source cross-field fallback");
    IniFile overlay;
    overlay.parse("[General]\nRepairStep=12\nBuildSpeed=invalid\nChronoTrigger=maybe\n"
                  "HoverHeight=99999999999999999999999\n");
    report = rules.read_file(overlay);
    require(report.invalid_keys.size() == 3, "invalid and overflowing rules reported");
    require(rules.get<double>("BuildSpeed") == static_cast<double>(0.7f), "invalid float retains prior");
    require(rules.get<std::int32_t>("RepairStep") == 12, "overlay replaces specified rule");
    require(rules.get<std::vector<std::int32_t>>("TeamDelays")->size() == 4, "overlay preserves absent list");
    IniFile no_general;
    no_general.parse("[Other]\nx=y\n");
    require(!rules.read_file(no_general).section_present, "missing General report");
    require(rules.get<std::int32_t>("RepairStep") == 12, "missing General changes nothing");
    rules.init(no_general);
    require(rules.get<double>("BuildSpeed") == 1.0, "init restores constructor slice");
}

void stock_checks(const IniFile& ini, const RulesReader& rules) {
    require(ini.statistics().section_headers == 1478, "stock section header count");
    require(ini.section_count() == 1452, "stock nonempty section count");
    require(ini.statistics().entries == 23176, "stock entry count");
    require(ini.key_count("General") == 378, "stock General key count");
    require(ini.statistics().duplicate_keys == 0 && ini.statistics().duplicate_sections == 0,
            "stock has no ambiguous duplicates");
    require(ini.read_list("General", "RepairBay") ==
                std::vector<std::string>({"GADEPT", "NADEPT", "CAOUTP"}), "stock semicolon truncation");
    require(ini.read_string("General", "RepairPercent") == "15%", "stock raw percentage");
    require(rules.get<double>("RepairPercent") == 0.15, "stock typed percentage");
    require(rules.get<std::int32_t>("SelfHealInfantryFrames") == 50, "stock infantry heal frames");
    require(rules.get<std::int32_t>("SelfHealUnitAmount") == 5, "stock unit heal amount");
    require(rules.get<double>("VeteranArmor") == 1.5, "stock veteran armor");
    require(rules.get<double>("HoverDampen") == 0.4, "stock hover percentage");
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool verify_stock = argc == 1;
        bool show_deferred = false;
        std::string path = "analysis/extracted/config/rulesmd.ini";
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--verify-stock") verify_stock = true;
            else if (argument == "--list-deferred") show_deferred = true;
            else if (argument == "--help") {
                std::cout << "Usage: rules_test [INI path] [--verify-stock] [--list-deferred]\n";
                return 0;
            } else if (argument.rfind("--", 0) == 0) {
                throw std::runtime_error("Unknown option: " + argument);
            } else path = argument;
        }
        parser_checks();
        rules_checks();
        const auto ini = IniFile::from_file(path);
        RulesReader rules;
        const auto report = rules.init(ini);
        if (verify_stock) stock_checks(ini, rules);
        std::cout << path << '\n'
                  << "section headers=" << ini.statistics().section_headers
                  << " nonempty sections=" << ini.section_count()
                  << " keys=" << ini.statistics().entries << '\n';
        for (const auto* section : {"General", "InfantryTypes", "VehicleTypes", "AircraftTypes",
                                    "BuildingTypes", "Warheads", "SuperWeaponTypes"}) {
            std::cout << '[' << section << "] keys=" << ini.key_count(section) << '\n';
        }
        std::cout << "General schema=" << RulesReader::general_schema().size()
                  << " loaded=" << report.loaded << " retained=" << report.retained
                  << " invalid=" << report.invalid_keys.size()
                  << " deferred=" << report.unhandled_keys.size() << '\n';
        for (const auto& key : report.invalid_keys) std::cout << "invalid: " << key << '\n';
        if (show_deferred) {
            for (const auto& key : report.unhandled_keys) std::cout << "deferred: " << key << '\n';
        }
        std::cout << "parser/overlay checks passed" << (verify_stock ? "; stock checks passed" : "") << '\n';
        return report.invalid_keys.empty() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
