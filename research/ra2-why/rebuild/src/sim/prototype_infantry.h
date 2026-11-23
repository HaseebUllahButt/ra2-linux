#pragma once

#include "../rules/ini_file.h"

namespace ra2::sim {
// Authored E1/E2 values for the first playable base skirmish. This helper does
// not convert Speed into movement, scale health, or execute weapons.
struct PrototypeInfantryRules {
    std::string type;
    std::int32_t speed = 0, strength = 0;
    std::string primary, secondary, armor;

    // Pass the loaded base RULES.INI, with any intended map overrides applied.
    // Throws for unsupported types or missing/invalid required fields.
    static PrototypeInfantryRules load(const rules::IniFile& rules, std::string_view type);
};
}
