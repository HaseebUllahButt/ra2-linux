#include "prototype_infantry.h"
#include "../fs/mix_bootstrap.h"

#include <iostream>
#include <stdexcept>

namespace {
using namespace ra2;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_failure(const rules::IniFile& rules, std::string_view type) {
    bool failed = false;
    try { sim::PrototypeInfantryRules::load(rules, type); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed, "Invalid infantry rules were accepted");
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: prototype_infantry_test <game-dir>");
    rules::IniFile fixture;
    fixture.parse("[E1]\nStrength=137\nSpeed=7\nPrimary=CustomGun\nSecondary=OtherGun\nArmor=plate\n"
                  "[E2]\nStrength=93\nSpeed=3\nPrimary=Rifle\nArmor=none\n");
    const auto first = sim::PrototypeInfantryRules::load(fixture, "E1");
    require(first.type == "E1" && first.strength == 137 && first.speed == 7 &&
            first.primary == "CustomGun" && first.secondary == "OtherGun" && first.armor == "plate",
            "Infantry values did not come from the supplied rules");
    require(sim::PrototypeInfantryRules::load(fixture, "E2").secondary.empty(),
            "Infantry acquired an unauthored secondary weapon");
    require_failure(fixture, "ENGINEER");
    rules::IniFile missing;
    require_failure(missing, "E1");
    for (const auto* fields : {"Strength=0\nSpeed=4\nPrimary=M60\nArmor=none\n",
                               "Strength=125\nSpeed=-1\nPrimary=M60\nArmor=none\n",
                               "Strength=125\nSpeed=4\nArmor=none\n",
                               "Strength=125\nSpeed=4\nPrimary=M60\n"}) {
        rules::IniFile invalid;
        invalid.parse(std::string("[E1]\n") + fields);
        require_failure(invalid, "E1");
    }

    fs::MixFileSystem files(argv[1]);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    const auto bytes = files.read_file("RULES.INI");
    require(bytes.has_value(), "Original base RULES.INI is missing");
    rules::IniFile original;
    original.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
    const auto gi = sim::PrototypeInfantryRules::load(original, "E1");
    const auto conscript = sim::PrototypeInfantryRules::load(original, "E2");
    require(gi.speed == 4 && gi.strength == 125 && gi.primary == "M60" &&
            gi.secondary == "Para" && gi.armor == "none", "Base GI rules changed");
    require(conscript.speed == 4 && conscript.strength == 125 && conscript.primary == "M1Carbine" &&
            conscript.secondary.empty() && conscript.armor == "flak", "Base Conscript rules changed");
    std::cout << "Base infantry rules passed: loaded values, optional secondary, and required fields.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
