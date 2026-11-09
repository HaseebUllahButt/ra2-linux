#include "prototype_yuri.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
// rulesmd.ini names are case sensitive, but country and armor names compare
// the way the original compares CRC names: case insensitive.
bool equal_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto upper = [](char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
        };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

bool list_contains(const std::string& list, std::string_view name) {
    for (const auto& item : rules::IniFile::split_list(list))
        if (equal_ignore_case(item, name)) return true;
    return false;
}

// The original warhead Verses= order. rulesmd.ini lists eleven entries.
constexpr std::array<std::string_view, 11> armor_names{
    "none", "flak", "plate", "light", "medium", "heavy",
    "wood", "steel", "concrete", "special_1", "special_2"};

double verse_value(const std::vector<double>& verses, std::string_view armor) {
    for (std::size_t i = 0; i < armor_names.size(); ++i)
        if (equal_ignore_case(armor_names[i], armor) && i < verses.size())
            return verses[i];
    return 1.0;
}

std::vector<double> read_verses(const rules::IniFile& rules, std::string_view warhead) {
    std::vector<double> result;
    for (const auto& item : rules.read_list(warhead, "Verses"))
        result.push_back(rules::IniFile::double_value(item).value_or(1.0));
    return result;
}

YuriMindControl read_mind_control(const rules::IniFile& rules, const char* weapon) {
    YuriMindControl result;
    result.name = weapon;
    result.nodes = rules.read_integer(weapon, "Damage", 0);
    result.infinite = rules.read_bool(weapon, "InfiniteMindControl", false);
    result.rof = rules.read_integer(weapon, "ROF", 0);
    result.range = static_cast<std::int32_t>(rules.read_double(weapon, "Range", 0.0));
    result.warhead = rules.read_string(weapon, "Warhead");
    result.verses = read_verses(rules, result.warhead);
    if (!rules.find_section(result.warhead) ||
        !rules.read_bool(result.warhead, "MindControl", false))
        throw std::runtime_error(std::string(weapon) + " has no mind control warhead");
    return result;
}

YuriGattlingWeapon read_weapon(const rules::IniFile& rules, std::string_view name) {
    const std::string section{name};
    YuriGattlingWeapon result;
    result.name = section;
    result.damage = rules.read_integer(section, "Damage", 0);
    result.rof = rules.read_integer(section, "ROF", 0);
    result.range = static_cast<std::int32_t>(rules.read_double(section, "Range", 0.0));
    result.warhead = rules.read_string(section, "Warhead");
    result.projectile = rules.read_string(section, "Projectile");
    if (rules.find_section(result.projectile)) {
        result.anti_air = rules.read_bool(result.projectile, "AA", false);
        // The original projectile defaults to ground attacks.
        result.anti_ground = rules.read_bool(result.projectile, "AG", true);
    } else {
        result.anti_ground = true;
    }
    return result;
}

// Weapon1..Weapon6 and EliteWeapon1..6 come in ground/air stage pairs.
std::vector<YuriGattling::Stage> read_stages(const rules::IniFile& rules,
                                             const char* type, bool elite) {
    std::vector<YuriGattling::Stage> stages;
    for (int pair = 0; pair < 3; ++pair) {
        YuriGattling::Stage stage;
        const std::string ground = (elite ? "EliteWeapon" : "Weapon") +
                                   std::to_string(pair * 2 + 1);
        const std::string air = (elite ? "EliteWeapon" : "Weapon") +
                                std::to_string(pair * 2 + 2);
        stage.ground = read_weapon(rules, rules.read_string(type, ground));
        stage.air = read_weapon(rules, rules.read_string(type, air));
        if (stage.ground.name.empty() || stage.air.name.empty()) continue;
        stages.push_back(std::move(stage));
    }
    return stages;
}

YuriGattling read_gattling(const rules::IniFile& rules, const char* type) {
    YuriGattling result;
    result.stages = read_stages(rules, type, false);
    result.elite_stages = read_stages(rules, type, true);
    result.rate_up = rules.read_integer(type, "RateUp", 1);
    result.rate_down = rules.read_integer(type, "RateDown", 50);
    return result;
}

YuriTechno read_techno(const rules::IniFile& rules, const rules::IniFile& art,
                       YuriTechno::Kind kind, const char* type) {
    YuriTechno result;
    result.kind = kind;
    result.name = type;
    const auto ui = rules.read_string(type, "UIName");
    // UIName=Name:Lasher points at the CSF label; keep the readable part.
    if (ui.rfind("Name:", 0) == 0) result.ui_name = ui.substr(5);
    result.owner = rules.read_string(type, "Owner");
    result.primary = rules.read_string(type, "Primary");
    result.secondary = rules.read_string(type, "Secondary");
    result.elite_primary = rules.read_string(type, "ElitePrimary");
    result.elite_secondary = rules.read_string(type, "EliteSecondary");
    result.armor = rules.read_string(type, "Armor");
    result.prerequisite = rules.read_string(type, "Prerequisite");
    result.image = rules.read_string(type, "Image");
    result.deploys_into = rules.read_string(type, "DeploysInto");
    result.undeploys_into = rules.read_string(type, "UndeploysInto");
    result.deployed_weapon = rules.read_string(type, "DeployedWeapon");
    result.enslaves = rules.read_string(type, "Enslaves");
    result.strength = rules.read_integer(type, "Strength", 0);
    result.cost = rules.read_integer(type, "Cost", 0);
    result.sight = rules.read_integer(type, "Sight", 0);
    result.speed = rules.read_integer(type, "Speed", 0);
    result.tech_level = rules.read_integer(type, "TechLevel", 0);
    result.build_limit = rules.read_integer(type, "BuildLimit", 0);
    result.naval = rules.read_bool(type, "Naval", false);
    result.gattling = rules.read_bool(type, "IsGattling", false);
    result.immune_to_psionics = rules.read_bool(type, "ImmuneToPsionics", false);
    result.deploy_to_fire = rules.read_bool(type, "DeployToFire", false);
    result.jumpjet = rules.read_bool(type, "JumpJet", false);
    result.cloakable = rules.read_bool(type, "Cloakable", false);
    result.grinding = rules.read_bool(type, "Grinding", false);
    result.powered = rules.read_bool(type, "Powered", false);
    result.passengers = rules.read_integer(type, "Passengers", 0);
    result.power = rules.read_integer(type, "Power", 0);
    result.trainable = rules.read_bool(type, "Trainable", false);
    result.voxel = art.read_bool(type, "Voxel", false);
    result.foundation = art.read_string(type, "Foundation");
    result.buildup = art.read_string(type, "Buildup");
    result.cameo = art.read_string(type, "Cameo");
    return result;
}

std::vector<std::int32_t> read_int_list(const rules::IniFile& rules,
                                        const char* key) {
    std::vector<std::int32_t> result;
    for (const auto& item : rules.read_list("General", key))
        result.push_back(rules::IniFile::integer_value(item).value_or(0));
    return result;
}

bool registered(const rules::IniFile& rules, const char* registry, const char* type) {
    const auto* section = rules.find_section(registry);
    if (!section) return false;
    return std::any_of(section->entries.begin(), section->entries.end(),
                       [&](const rules::IniFile::Entry& entry) {
                           return equal_ignore_case(entry.value, type);
                       });
}
} // namespace

bool YuriTechno::owned_by(std::string_view country) const {
    return list_contains(owner, country);
}

std::size_t YuriOverload::category(std::int32_t controlled) const {
    if (count.empty()) return 0;
    for (std::size_t i = 0; i < count.size(); ++i)
        if (controlled <= count[i]) return i;
    return count.size() - 1;
}

std::int32_t YuriOverload::damage_for(std::int32_t controlled) const {
    if (damage.empty()) return 0;
    return damage[category(controlled)];
}

std::int32_t YuriOverload::frames_for(std::int32_t controlled) const {
    if (frames.empty()) return 0;
    return frames[category(controlled)];
}

bool YuriMindControl::controls_buildings() const {
    return verse_for("wood") > 0.0 && verse_for("steel") > 0.0 &&
           verse_for("concrete") > 0.0;
}

double YuriMindControl::verse_for(std::string_view armor) const {
    return verse_value(verses, armor);
}

void GattlingState::tick(bool firing, bool elite) {
    const auto& values = elite ? rules_->elite_stage_values : rules_->stage_values;
    const auto stages = static_cast<std::int32_t>(rules_->stages.size());
    if (stages == 0) return;
    if (firing) {
        // 0070DE70: the value climbs by RateUp under the stage threshold.
        const auto threshold = values[static_cast<std::size_t>(stage_)];
        if (value_ < threshold) value_ += rules_->rate_up;
        // Crossing the threshold selects the next stage weapon pair.
        if (stage_ < stages - 1 && value_ >= values[static_cast<std::size_t>(stage_)]) {
            ++stage_;
            value_ = 0;
        }
    } else {
        // 0070E000: the value cools by RateDown and drops a stage when the
        // value falls below its threshold.
        value_ -= rules_->rate_down;
        if (value_ < 0) value_ = 0;
        if (stage_ > 0 && value_ < values[static_cast<std::size_t>(stage_)]) --stage_;
    }
}

std::int32_t GattlingState::weapon_number(bool air_target) const {
    return stage_ * 2 + (air_target ? 1 : 0);
}

const YuriGattlingWeapon* GattlingState::weapon(bool air_target, bool elite) const {
    const auto& table = elite ? rules_->elite_stages : rules_->stages;
    if (table.empty()) return nullptr;
    const auto index = static_cast<std::size_t>(stage_) < table.size()
                           ? static_cast<std::size_t>(stage_)
                           : 0;
    const auto& stage = table[index];
    return air_target ? &stage.air : &stage.ground;
}

bool MindController::controls(std::uint32_t target) const {
    return std::any_of(nodes_.begin(), nodes_.end(),
                       [&](const Node& node) { return node.target == target; });
}

bool MindController::can_capture(bool same_abstract_type, double verse, bool immune,
                                 bool already_controlled, bool permanent_target) const {
    if (same_abstract_type) return false;   // A tower cannot control a tower.
    if (verse <= 0.0) return false;         // The warhead cannot hurt this armor.
    if (immune) return false;               // Immune and ImmuneToPsionics targets.
    // A captured target can be stolen only when the new link is permanent.
    if (already_controlled && !permanent_target) return false;
    return true;
}

bool MindController::capture(std::uint32_t target, std::uint32_t original_owner) {
    if (target == 0 || controls(target)) return false;
    // 00471D40: a full controller releases its links before the new capture.
    if (full() && !infinite_) free_all();
    if (full()) return false; // A Mastermind at its limit only overloads.
    nodes_.push_back({target, original_owner});
    return true;
}

std::uint32_t MindController::free(std::uint32_t target) {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->target == target) {
            const auto owner = it->original_owner;
            nodes_.erase(it);
            return owner;
        }
    }
    return 0;
}

void MindController::free_all() { nodes_.clear(); }

std::int32_t MindController::overload_tick() {
    if (!overloaded()) {
        pip_state_ = 0;
        damage_delay_ = 0;
        return 0;
    }
    const auto controlled = static_cast<std::int32_t>(nodes_.size());
    pip_state_ = static_cast<std::int32_t>(overload_.category(controlled));
    const auto frames = overload_.frames_for(controlled);
    if (frames <= 0) return 0;
    if (damage_delay_ > 0) {
        --damage_delay_;
        return 0;
    }
    damage_delay_ = frames;
    return overload_.damage_for(controlled);
}

void MagnetronBeam::start(std::uint32_t source, std::uint32_t target) {
    source_ = source;
    target_ = target;
    active_ = target != 0 && source != 0;
}

bool MagnetronBeam::drag(ra2map::Cell& target_cell, ra2map::Cell source_cell) {
    if (!active_) return false;
    const auto step = [](std::int16_t current, std::int16_t goal) {
        return static_cast<std::int16_t>(
            current + (goal < current ? -1 : goal > current ? 1 : 0));
    };
    const std::uint16_t next_x = step(target_cell.x, source_cell.x);
    const std::uint16_t next_y = step(target_cell.y, source_cell.y);
    const bool arrived = next_x == source_cell.x && next_y == source_cell.y;
    target_cell = {next_x, next_y};
    if (arrived) active_ = false;
    return !arrived;
}

std::int32_t MagnetronBeam::drop_damage(std::int32_t target_strength,
                                        std::int32_t target_max_strength,
                                        double falling_multiplier,
                                        bool current_strength_damage) const {
    const auto base = current_strength_damage ? target_strength : target_max_strength;
    const auto damage = static_cast<std::int32_t>(base * falling_multiplier);
    return damage > 0 ? damage : 0;
}

void DiscDrain::start(std::uint32_t building) {
    target_ = building;
    elapsed_ = 0;
}

std::int32_t DiscDrain::tick(std::int32_t frame_delay, std::int32_t amount,
                             std::int32_t building_credits) {
    if (target_ == 0 || building_credits <= 0 || frame_delay <= 0) return 0;
    if (++elapsed_ < frame_delay) return 0;
    elapsed_ = 0;
    return amount < building_credits ? amount : building_credits;
}

const char* BoomerState::weapon(bool target_is_naval) {
    return target_is_naval ? "BoomerTorpedo" : "CruiseLauncher";
}

const char* SiegeChopperState::weapon(const std::string& deployed_weapon) const {
    return deployed_ && !deployed_weapon.empty() ? deployed_weapon.c_str() : nullptr;
}

std::int32_t grinder_refund(const YuriTechno& unit, std::int32_t refund_percent) {
    if (unit.kind == YuriTechno::Kind::Building) return 0;
    const auto refund = static_cast<std::int64_t>(unit.cost) * refund_percent / 100;
    return static_cast<std::int32_t>(refund);
}

bool cloning_vats_duplicates(const YuriTechno& trained) {
    return trained.kind == YuriTechno::Kind::Infantry;
}

const char* guardian_gi_weapon(bool deployed, const std::string& primary,
                               const std::string& secondary) {
    return deployed && !secondary.empty() ? secondary.c_str() : primary.c_str();
}

bool robot_tank_needs_center(const YuriTechno& tank, const YuriTechno& center) {
    return tank.kind == YuriTechno::Kind::Unit &&
           center.kind == YuriTechno::Kind::Building &&
           equal_ignore_case(center.name, "GAROBO");
}

bool robot_tank_immune_to_psionics(const YuriTechno& tank) {
    return tank.immune_to_psionics;
}

PrototypeYuriRules PrototypeYuriRules::load(const rules::IniFile& rules_md,
                                            const rules::IniFile& art_md) {
    PrototypeYuriRules result;
    // [Sides] ThirdSide=YuriCountry. The side index counts the combat rows
    // before ThirdSide; Civilian and Mutant are special, not combat sides.
    const auto* sides = rules_md.find_section("Sides");
    if (!sides) throw std::runtime_error("rulesmd.ini has no [Sides]");
    bool found = false;
    for (const auto& entry : sides->entries) {
        if (equal_ignore_case(entry.key, "Civilian") ||
            equal_ignore_case(entry.key, "Mutant"))
            continue;
        if (rules::IniFile::split_list(entry.value).empty()) continue;
        if (equal_ignore_case(entry.key, "ThirdSide")) {
            result.side_name = entry.key;
            result.country = entry.value;
            found = true;
            break;
        }
        ++result.side_index;
    }
    if (!found || result.country.empty())
        throw std::runtime_error("rulesmd.ini [Sides] has no ThirdSide");
    // [Countries] lists the playable countries in order.
    const auto* countries = rules_md.find_section("Countries");
    if (!countries) throw std::runtime_error("rulesmd.ini has no [Countries]");
    for (const auto& entry : countries->entries)
        if (equal_ignore_case(entry.value, result.country))
            result.country_index =
                rules::IniFile::integer_value(entry.key).value_or(0);
    const auto* country = rules_md.find_section(result.country);
    if (!country) throw std::runtime_error("rulesmd.ini has no [" + result.country + "]");
    result.country_suffix = rules_md.read_string(result.country, "Suffix");

    // Yuri side content, in rules order.
    for (const auto* registry : {"InfantryTypes", "VehicleTypes", "BuildingTypes"}) {
        const auto* section = rules_md.find_section(registry);
        if (!section)
            throw std::runtime_error(std::string("rulesmd.ini has no [") + registry + "]");
        const auto kind = std::string_view(registry) == "InfantryTypes"
                              ? YuriTechno::Kind::Infantry
                              : std::string_view(registry) == "VehicleTypes"
                                    ? YuriTechno::Kind::Unit
                                    : YuriTechno::Kind::Building;
        for (const auto& entry : section->entries) {
            const auto& type = entry.value;
            if (!rules_md.find_section(type)) continue;
            YuriTechno techno = read_techno(rules_md, art_md, kind, type.c_str());
            if (!techno.yuri()) continue;
            if (kind == YuriTechno::Kind::Infantry)
                result.infantry.push_back(std::move(techno));
            else if (kind == YuriTechno::Kind::Unit)
                result.vehicles.push_back(std::move(techno));
            else
                result.buildings.push_back(std::move(techno));
        }
    }
    if (result.buildings.empty()) throw std::runtime_error("Yuri side has no buildings");

    // Special rule values.
    result.overload.count = read_int_list(rules_md, "OverloadCount");
    result.overload.damage = read_int_list(rules_md, "OverloadDamage");
    result.overload.frames = read_int_list(rules_md, "OverloadFrames");
    result.mind_control = read_mind_control(rules_md, "MindControl");
    result.super_mind_control = read_mind_control(rules_md, "SuperMindControl");
    result.multi_tank_control = read_mind_control(rules_md, "MultipleMindControlTank");
    result.multi_tower_control = read_mind_control(rules_md, "MultipleMindControlTower");
    result.drain_delay = rules_md.read_integer("General", "DrainMoneyFrameDelay", 0);
    result.drain_amount = rules_md.read_integer("General", "DrainMoneyAmount", 0);
    result.falling_damage_multiplier =
        rules_md.read_double("General", "FallingDamageMultiplier", 1.0);
    result.current_strength_damage =
        rules_md.read_bool("General", "CurrentStrengthDamage", true);
    // RefundPercent=50% parses to 0.5 through the original percent rule.
    result.refund_percent = static_cast<std::int32_t>(
        rules::IniFile::double_value(rules_md.read_string("General", "RefundPercent"))
            .value_or(0.5) * 100.0);
    result.open_topped.range_bonus =
        rules_md.read_integer("General", "OpenToppedRangeBonus", 0);
    result.open_topped.damage_multiplier =
        rules_md.read_double("General", "OpenToppedDamageMultiplier", 1.0);
    result.open_topped.warp_distance =
        rules_md.read_integer("General", "OpenToppedWarpDistance", 0);

    // New Allied and Soviet expansion units. SCHP is the Soviet Siege
    // Chopper; GGI, ROBO and BFRT are the Allied ones.
    for (const auto* name : {"GGI", "ROBO", "BFRT"}) {
        const auto kind = std::string(name) == "GGI" ? YuriTechno::Kind::Infantry
                                                     : YuriTechno::Kind::Unit;
        const auto& registry = kind == YuriTechno::Kind::Infantry
                                   ? "InfantryTypes"
                                   : "VehicleTypes";
        if (registered(rules_md, registry, name))
            result.allied_units.push_back(
                read_techno(rules_md, art_md, kind, name));
    }
    if (registered(rules_md, "VehicleTypes", "SCHP"))
        result.soviet_units.push_back(
            read_techno(rules_md, art_md, YuriTechno::Kind::Unit, "SCHP"));

    // The gattling stage pairs come from the two gattling types.
    const auto* tank = result.find_vehicle("YTNK");
    const auto* cannon = result.find_building("YAGGUN");
    if (!tank || !cannon) throw std::runtime_error("Yuri gattling types are missing");
    result.gattling_tank = read_gattling(rules_md, tank->name.c_str());
    result.gattling_cannon = read_gattling(rules_md, cannon->name.c_str());
    return result;
}

const YuriTechno* PrototypeYuriRules::find(std::string_view name) const {
    const auto search = [&](const std::vector<YuriTechno>& list) -> const YuriTechno* {
        for (const auto& techno : list)
            if (equal_ignore_case(techno.name, name)) return &techno;
        return nullptr;
    };
    for (const auto* list : {&infantry, &vehicles, &buildings, &allied_units, &soviet_units})
        if (const auto* found = search(*list)) return found;
    return nullptr;
}

const YuriTechno* PrototypeYuriRules::find_infantry(std::string_view name) const {
    for (const auto& techno : infantry)
        if (equal_ignore_case(techno.name, name)) return &techno;
    return nullptr;
}

const YuriTechno* PrototypeYuriRules::find_vehicle(std::string_view name) const {
    for (const auto& techno : vehicles)
        if (equal_ignore_case(techno.name, name)) return &techno;
    return nullptr;
}

const YuriTechno* PrototypeYuriRules::find_building(std::string_view name) const {
    for (const auto& techno : buildings)
        if (equal_ignore_case(techno.name, name)) return &techno;
    return nullptr;
}

} // namespace ra2::sim
