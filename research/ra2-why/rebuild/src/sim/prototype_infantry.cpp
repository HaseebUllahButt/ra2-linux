#include "prototype_infantry.h"

#include <stdexcept>

namespace ra2::sim {
PrototypeInfantryRules PrototypeInfantryRules::load(const rules::IniFile& rules,
                                                   std::string_view type) {
    if (type != "E1" && type != "E2")
        throw std::runtime_error("Infantry prototype supports E1 and E2");
    PrototypeInfantryRules result;
    result.type = type;
    result.speed = rules.read_integer(type, "Speed", 0);
    result.strength = rules.read_integer(type, "Strength", 0);
    result.primary = rules.read_string(type, "Primary");
    result.secondary = rules.read_string(type, "Secondary");
    result.armor = rules.read_string(type, "Armor");
    if (result.speed <= 0 || result.strength <= 0 || result.primary.empty() || result.armor.empty())
        throw std::runtime_error("Missing or invalid infantry rules: " + std::string(type));
    return result;
}
}
