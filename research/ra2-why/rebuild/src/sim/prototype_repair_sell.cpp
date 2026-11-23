#include "prototype_repair_sell.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
using WorldEntity = session::world::Entity;
using WorldHouse = session::world::House;
using WorldKind = session::world::Kind;
using session::world::CreatedKind;
using session::world::Layer;
using session::world::Lifetime;
using session::world::SetupProgress;
using World = session::world::Snapshot;

constexpr int ticks_per_minute = 900; // Original TICKS_PER_MINUTE.

WorldEntity* find_entity(World& world, std::uint32_t id) {
    const auto it = std::find_if(world.entities.begin(), world.entities.end(),
        [&](const WorldEntity& entity) { return entity.id == id; });
    return it == world.entities.end() ? nullptr : &*it;
}

WorldHouse* find_house(World& world, std::uint32_t id) {
    const auto it = std::find_if(world.houses.begin(), world.houses.end(),
        [&](const WorldHouse& house) { return house.id == id; });
    return it == world.houses.end() ? nullptr : &*it;
}

// Original side order: 0 Allied, 1 Soviet, 2 third. Base game.exe 0x0044C520
// picks the Soviet divisor (RulesClass +0x1164) only for a side value of 1;
// every other value picks the Allied divisor (+0x1160). The parser
// FUN_00647F40 reads both divisors; there is no third divisor.
std::optional<std::int32_t> house_side(const WorldHouse& house, const rules::IniFile& rules) {
    if (house.side_index) return house.side_index;
    const auto* sides = rules.find_section("Sides");
    if (!sides) return {};
    const auto side = rules.read_string(house.country, "Side");
    for (std::size_t i = 0; i < sides->entries.size(); ++i) {
        const auto& entry = sides->entries[i];
        if (rules::IniFile::trim(entry.key) == side ||
            rules::IniFile::trim(entry.value) == house.country)
            return static_cast<std::int32_t>(i);
    }
    return {};
}

bool live_structure(const WorldEntity* entity) {
    return entity && entity->kind == WorldKind::Structure &&
        entity->lifetime == Lifetime::Placed && entity->cell &&
        entity->resolved_hp.value_or(0) > 0;
}

// Resolved maximum health: the staged value, else the type Strength.
std::int32_t maximum_health(const WorldEntity& entity, const rules::IniFile& rules) {
    if (entity.maximum_hp && *entity.maximum_hp > 0) return *entity.maximum_hp;
    const auto strength = rules.read_integer(entity.type, "Strength", 0);
    return strength > 0 ? strength : 0;
}

// Foundation size from art.ini "Foundation=WxH". A missing or malformed value
// is a one cell footprint. TODO-ORIGINAL: bib cells and the original
// foundation reader are not modeled.
std::pair<int, int> foundation(const rules::IniFile& art, const std::string& type) {
    const auto value = art.read_string(type, "Foundation");
    const auto cross = value.find('x');
    if (cross == std::string::npos) return {1, 1};
    const auto width = rules::IniFile::integer_value(rules::IniFile::trim(value.substr(0, cross)));
    const auto height = rules::IniFile::integer_value(rules::IniFile::trim(value.substr(cross + 1)));
    if (!width || !height || *width < 1 || *height < 1 || *width > 32 || *height > 32) return {1, 1};
    return {*width, *height};
}

std::uint32_t cell_key(ra2map::Cell cell) { return std::uint32_t(cell.y) * 512 + cell.x; }

// A cell is open when no live actor stands on it and the staged Cell state,
// when known, allows ground entry. Mirrors the movement prototype checks.
// actors maps Cell keys to placed Unit, Infantry or Structure ids; structure
// entries cover the whole foundation, not only the anchor Cell.
bool cell_open(const World& world, ra2map::Cell cell,
               const std::map<std::uint32_t, std::uint32_t>& actors) {
    if (cell.x >= 512 || cell.y >= 512) return false;
    const auto key = cell_key(cell);
    if (!world.cells.empty()) {
        // The staged setup owns the complete Cell list; a missing record is
        // blocked, as in the movement prototype.
        const auto found = world.cells.find(key);
        if (found == world.cells.end()) return false;
        const auto& state = found->second;
        if (state.allocated && !*state.allocated) return false;
        if (state.cell_flag_100 && *state.cell_flag_100) return false;
        if (state.unit_land_speed && *state.unit_land_speed <= 0) return false;
        if (state.heads && state.heads->ground_head) return false;
        if (state.ground_occupancy_124 && *state.ground_occupancy_124) return false;
    }
    return !actors.count(key);
}

// Remove one object id from every staged ground chain. The id must be a chain
// member; its own next pointer stitches the gap. A Cell whose chain emptied
// loses its ground occupancy byte.
void unlink_from_cells(World& world, std::uint32_t id, std::uint32_t next) {
    for (auto& item : world.cells) {
        auto& heads = item.second.heads;
        if (!heads || !heads->ground_head) continue;
        std::uint32_t* link = &heads->ground_head;
        std::set<std::uint32_t> seen;
        while (*link) {
            if (!seen.insert(*link).second) break; // Cyclic staged input: leave it alone.
            if (*link == id) {
                *link = next;
                break;
            }
            auto* entity = find_entity(world, *link);
            if (!entity || !entity->next || !*entity->next) break;
            link = &*entity->next;
        }
        // TODO-ORIGINAL: structure Cell occupancy bits and per-cell teardown
        // are not recovered. Clear the byte only when the whole chain emptied,
        // like the damage runtime does for destroyed units.
        if (!heads->ground_head && item.second.ground_occupancy_124)
            item.second.ground_occupancy_124 = 0;
    }
}

std::uint32_t next_entity_id(const World& world) {
    std::uint32_t largest = 0;
    for (const auto& house : world.houses) largest = std::max(largest, house.id);
    for (const auto& entity : world.entities) largest = std::max(largest, entity.id);
    return largest + 1;
}
} // namespace

PrototypeRepairSellRules PrototypeRepairSellRules::load(const rules::IniFile& rules) {
    PrototypeRepairSellRules result;
    result.repair_percent = rules.read_double("General", "RepairPercent", -1.0);
    result.repair_rate_minutes = rules.read_double("General", "RepairRate", -1.0);
    result.repair_step = rules.read_integer("General", "RepairStep", 0);
    result.refund_percent = rules.read_double("General", "RefundPercent", -1.0);
    result.allied_survivor_divisor = rules.read_integer("General", "AlliedSurvivorDivisor", 0);
    result.soviet_survivor_divisor = rules.read_integer("General", "SovietSurvivorDivisor", 0);
    // The RULES parser FUN_00647F40 reads only the Allied and Soviet
    // survivor divisors (+0x1160 and +0x1164). The installed RULES.INI has
    // no third divisor and the count body 0x0044C520 reads none.
    if (result.repair_percent <= 0.0 || result.repair_percent > 1.0)
        throw std::runtime_error("Missing or invalid [General] RepairPercent");
    if (result.repair_rate_minutes <= 0.0)
        throw std::runtime_error("Missing or invalid [General] RepairRate");
    if (result.repair_step <= 0)
        throw std::runtime_error("Missing or invalid [General] RepairStep");
    if (result.refund_percent < 0.0 || result.refund_percent > 1.0)
        throw std::runtime_error("Missing or invalid [General] RefundPercent");
    if (result.allied_survivor_divisor <= 0 || result.soviet_survivor_divisor <= 0)
        throw std::runtime_error("Missing or invalid [General] survivor divisors");
    return result;
}

PrototypeRepairSell::PrototypeRepairSell(const rules::IniFile& base_rules,
    const rules::IniFile* art_rules)
    : rules_(base_rules), art_(art_rules), limits_(PrototypeRepairSellRules::load(base_rules)) {}

std::int32_t PrototypeRepairSell::repair_cost_per_step(const std::string& type) const {
    // Recovered from game.exe: the TechnoTypeClass vtable is 0x007AD080
    // (RTTI .?AVTechnoTypeClass@@); BuildingTypeClass 0x0079D1F8 inherits
    // the same slots. Slot +0xB0 is 0x006DAEC0, the per-step repair cost:
    // Cost (virtual +0xAC) divided by (Strength / RepairStep), times
    // RepairPercent (RulesClass +0x1338), truncated, with a minimum of 1.
    // Slot +0xB4 is 0x006DAF10, which returns RepairStep (RulesClass
    // +0x1334). A zero Cost divides safely and still clamps to 1.
    // TODO-ORIGINAL: this guard returns 0 for a type without Strength or
    // with a zero divisor; the original would divide by zero instead.
    const auto cost = rules_.read_integer(type, "Cost", 0);
    const auto strength = rules_.read_integer(type, "Strength", 0);
    if (strength <= 0) return 0;
    const auto divisor = strength / limits_.repair_step;
    if (divisor <= 0) return 0;
    const auto value = static_cast<std::int32_t>(
        static_cast<double>(cost / divisor) * limits_.repair_percent);
    return value < 1 ? 1 : value;
}

std::int32_t PrototypeRepairSell::refund_amount(const std::string& type) const {
    // Recovered from game.exe: the refund virtual is TechnoTypeClass vtable
    // 0x007AD080 slot +0xB8 = 0x006DAD80. It multiplies the Cost (virtual
    // +0xAC) by RefundPercent (RulesClass +0x13A0) and truncates. Its extra
    // arguments can force the full price and apply a house factor.
    // TODO-ORIGINAL: the house factor and full-price argument are not
    // modeled; this is the plain human-house shape.
    const auto cost = rules_.read_integer(type, "Cost", 0);
    if (cost <= 0) return 0;
    return static_cast<std::int32_t>(static_cast<double>(cost) * limits_.refund_percent);
}

std::int32_t PrototypeRepairSell::survivor_count(const session::world::House& house,
    const std::string& type) const {
    // Recovered from game.exe 0x0044C520 (BuildingClass vtable 0x0079CBBC
    // slot 172): the refund (type virtual +0xB8 with the owner and 0)
    // divided by the side divisor, clamped to 1..5, zero refund included.
    // Only a side value of 1 picks the Soviet divisor; every other value
    // picks the Allied one. A zero divisor returns 0. TODO-ORIGINAL: the
    // original gates on BuildingClass +0x5B0 and a BuildingTypeClass byte
    // at +0xAD9 (read through the building's type pointer at +0x418, so
    // likely Crewed) and doubles the divisor for a captured building
    // (+0x5B3); this keeps the Crewed rules check as its gate and does not
    // model capture.
    if (!rules_.read_bool(type, "Crewed")) return 0;
    const auto side = house_side(house, rules_);
    if (!side) return 0;
    const auto divisor = *side == 1 ? limits_.soviet_survivor_divisor
        : limits_.allied_survivor_divisor;
    if (divisor <= 0) return 0;
    const auto count = refund_amount(type) / divisor;
    return count < 1 ? 1 : count > 5 ? 5 : count;
}

const char* PrototypeRepairSell::crew_type(const session::world::House& house) const {
    const auto side = house_side(house, rules_);
    return !side ? "E1" : *side == 1 ? "E2" : *side == 2 ? "INIT" : "E1";
}

bool PrototypeRepairSell::set_repair(World& world, std::uint32_t building_id, bool on) {
    const bool active = repairing(building_id);
    if (!on) {
        repairing_.erase(std::remove(repairing_.begin(), repairing_.end(), building_id),
            repairing_.end());
        return true;
    }
    const auto* entity = find_entity(world, building_id);
    if (!live_structure(entity)) return false;
    const auto max = maximum_health(*entity, rules_);
    if (max <= 0 || !entity->resolved_hp || *entity->resolved_hp >= max) return false;
    if (!active) {
        repairing_.insert(std::upper_bound(repairing_.begin(), repairing_.end(), building_id),
            building_id);
    }
    return true;
}

bool PrototypeRepairSell::repairing(std::uint32_t building_id) const {
    return std::binary_search(repairing_.begin(), repairing_.end(), building_id);
}

PrototypeRepairSell::SellResult PrototypeRepairSell::sell(World& world, std::uint32_t building_id) {
    SellResult result;
    if (world.pending_cell_setup) {
        result.detail = "prototype sell needs a finished Cell setup";
        result.status = SellResult::Status::NeedsInput;
        return result;
    }
    if (world.setup_progress != SetupProgress::PrototypeComplete) {
        result.detail = "prototype sell needs a PrototypeComplete world";
        result.status = SellResult::Status::NeedsInput;
        return result;
    }
    auto* entity = find_entity(world, building_id);
    if (!live_structure(entity)) {
        result.detail = "prototype sell needs a placed, alive Structure";
        return result;
    }
    if (!entity->next) {
        result.detail = "prototype sell needs a resolved Object link";
        result.status = SellResult::Status::NeedsInput;
        return result;
    }
    auto* house = find_house(world, entity->house_id);
    if (!house) {
        result.detail = "prototype sell needs the owning House";
        return result;
    }
    if (!house->cash) {
        result.detail = "prototype sell needs resolved house cash";
        result.status = SellResult::Status::NeedsInput;
        return result;
    }

    // Placed actors by Cell key. Structures cover their whole foundation.
    const auto size = art_ ? foundation(*art_, entity->type) : std::make_pair(1, 1);
    std::map<std::uint32_t, std::uint32_t> actors;
    const auto add_footprint = [&](const WorldEntity& structure) {
        const auto span = art_ ? foundation(*art_, structure.type) : std::make_pair(1, 1);
        for (int dy = 0; dy < span.second; ++dy)
            for (int dx = 0; dx < span.first; ++dx) {
                const ra2map::Cell spot{static_cast<std::uint16_t>(structure.cell->x + dx),
                    static_cast<std::uint16_t>(structure.cell->y + dy)};
                if (spot.x < 512 && spot.y < 512) actors.emplace(cell_key(spot), structure.id);
            }
    };
    for (const auto& other : world.entities) {
        if (other.lifetime != Lifetime::Placed || !other.cell) continue;
        if (other.kind == WorldKind::Unit || other.kind == WorldKind::Infantry) {
            actors.emplace(cell_key(*other.cell), other.id);
        } else if (other.kind == WorldKind::Structure && other.id != building_id) {
            add_footprint(other);
        }
    }

    const auto refund = refund_amount(entity->type);
    *house->cash += refund;
    result.refund = refund;

    const auto next = *entity->next;
    const auto anchor = *entity->cell;
    unlink_from_cells(world, building_id, next);
    entity->lifetime = Lifetime::Destroyed;
    entity->resolved_hp = 0;
    repairing_.erase(std::remove(repairing_.begin(), repairing_.end(), building_id),
        repairing_.end());
    for (int dy = 0; dy < size.second; ++dy)
        for (int dx = 0; dx < size.first; ++dx) {
            const ra2map::Cell spot{static_cast<std::uint16_t>(anchor.x + dx),
                static_cast<std::uint16_t>(anchor.y + dy)};
            if (spot.x < 512 && spot.y < 512) actors.erase(cell_key(spot));
        }

    // Crew leaves the building, nearest free Cell first. A survivor without a
    // free Cell is dropped, as in the original failed Unlimbo path. The crew
    // walks out afterwards; the movement prototype owns that walk.
    const auto count = survivor_count(*house, entity->type);
    const auto crew = crew_type(*house);
    const auto crew_hp = rules_.read_integer(crew, "Strength", 100);
    const int center_x = anchor.x + (size.first - 1) / 2;
    const int center_y = anchor.y + (size.second - 1) / 2;
    for (int member = 0; member < count; ++member) {
        std::optional<ra2map::Cell> spot;
        for (int radius = 0; radius <= 16 && !spot; ++radius)
            for (int dy = -radius; dy <= radius && !spot; ++dy)
                for (int dx = -radius; dx <= radius && !spot; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                    const int x = center_x + dx, y = center_y + dy;
                    if (x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                    const ra2map::Cell candidate{static_cast<std::uint16_t>(x),
                        static_cast<std::uint16_t>(y)};
                    if (cell_open(world, candidate, actors)) spot = candidate;
                }
        if (!spot) break;
        const auto id = next_entity_id(world);
        WorldEntity infantry;
        infantry.id = id;
        infantry.house_id = house->id;
        infantry.type = crew;
        infantry.kind = WorldKind::Infantry;
        infantry.lifetime = Lifetime::Placed;
        infantry.cell = *spot;
        infantry.position = sim::Coord{spot->x * 256 + 128, spot->y * 256 + 128, 0};
        infantry.resolved_hp = crew_hp;
        infantry.maximum_hp = crew_hp;
        infantry.layer = Layer::Ground;
        infantry.next = 0;
        infantry.active = true;
        infantry.in_limbo = false;
        infantry.on_map = true;
        world.entities.push_back(std::move(infantry));
        world.creation_order.push_back({CreatedKind::Entity, id});
        actors.emplace(cell_key(*spot), id);
        // Join the staged ground chain when the Cell is staged. Infantry keeps
        // no subcell reservation here.
        // TODO-ORIGINAL: Infantry subcell mask and reservation.
        const auto staged = world.cells.find(cell_key(*spot));
        if (staged != world.cells.end() && staged->second.heads) {
            world.entities.back().next = staged->second.heads->ground_head;
            staged->second.heads->ground_head = id;
        }
        result.crew.push_back(id);
    }
    result.status = SellResult::Status::Complete;
    return result;
}

PrototypeRepairSell::RepairTick PrototypeRepairSell::tick(World& world, std::uint32_t game_frame) {
    RepairTick result;
    // Original interval, recovered from game.exe 0x0044BBA1: the shared
    // frame DAT_00A40D2C divided by trunc(RepairRate * 900.0), the 900.0
    // constant read from 0x0079B608, and a step runs only on a zero
    // remainder. The parser FUN_00647F40 stores RepairRate at RulesClass
    // +0x1348. TODO-ORIGINAL: this max(1, ...) guard replaces the
    // original's divide-by-zero on a zero interval.
    const auto interval = static_cast<std::uint32_t>(std::max(1,
        static_cast<std::int32_t>(limits_.repair_rate_minutes * limits_.ticks_per_minute)));
    if (last_frame_) {
        if (game_frame == *last_frame_) return result; // Repeated frame.
        if (game_frame < *last_frame_) {               // Rewound frame: forget it.
            last_frame_ = game_frame;
            return result;
        }
    }
    last_frame_ = game_frame;
    if (game_frame % interval != 0) return result;
    for (auto it = repairing_.begin(); it != repairing_.end();) {
        const auto id = *it;
        auto* entity = find_entity(world, id);
        if (!live_structure(entity)) {
            it = repairing_.erase(it);
            continue;
        }
        const auto max = maximum_health(*entity, rules_);
        if (max <= 0 || *entity->resolved_hp >= max) {
            if (max > 0) entity->resolved_hp = max;
            it = repairing_.erase(it);
            continue;
        }
        const auto cost = repair_cost_per_step(entity->type);
        auto* house = find_house(world, entity->house_id);
        // A house without resolved cash, or too poor, stops repairing, as in
        // the original per-tick money check.
        if (!house || !house->cash || *house->cash < cost) {
            it = repairing_.erase(it);
            continue;
        }
        *house->cash -= cost;
        result.spent += cost;
        *entity->resolved_hp = std::min(max, *entity->resolved_hp + limits_.repair_step);
        result.repaired.push_back(id);
        if (*entity->resolved_hp >= max) {
            entity->resolved_hp = max;
            it = repairing_.erase(it);
            continue;
        }
        ++it;
    }
    return result;
}
} // namespace ra2::sim
