#include "prototype_superweapon.h"

#include "combat.h"
#include "distance.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
using rules::IniFile;
namespace w = session::world;
using w::Entity;
using w::Snapshot;

// The original INI lookup compares names case-insensitively (FUN_007C8D20).
bool same_id(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; };
        if (upper(a[i]) != upper(b[i])) return false;
    }
    return true;
}

std::string lower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - ('A' - 'a')) : c);
    return out;
}

const IniFile::Section* find_section(const IniFile& rules, std::string_view name) {
    if (const auto* exact = rules.find_section(name)) return exact;
    for (const auto& section : rules.sections())
        if (same_id(section.name, name)) return &section;
    return nullptr;
}

// Original Object state this prototype treats as alive, matching the shared
// combat prototype guards.
bool alive(const Entity* entity) {
    return entity && entity->id &&
        (entity->kind == w::Kind::Unit || entity->kind == w::Kind::Infantry ||
         entity->kind == w::Kind::Structure || entity->kind == w::Kind::Aircraft) &&
        entity->lifetime == w::Lifetime::Placed && entity->position &&
        (!entity->resolved_hp || *entity->resolved_hp > 0) &&
        (!entity->active || *entity->active) && (!entity->in_limbo || !*entity->in_limbo);
}

Entity* find_entity(Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [id](const Entity& entity) { return entity.id == id; });
    return found == world.entities.end() ? nullptr : &*found;
}

std::uint32_t cell_key(ra2map::Cell cell) {
    return static_cast<std::uint32_t>(cell.y) * 512 + cell.x;
}

bool in_map(ra2map::Cell cell) { return cell.x < 512 && cell.y < 512; }

Coord cell_center(ra2map::Cell cell) {
    return Coord{cell.x * 256 + 128, cell.y * 256 + 128, 0};
}

// TODO-ORIGINAL: ramp and bridge floor heights stay zero in this prototype.
int floor_z(const Snapshot& world, ra2map::Cell cell) {
    const auto found = world.cells.find(cell_key(cell));
    if (found == world.cells.end() || !found->second.terrain || !found->second.terrain->floor_world_height)
        return 0;
    return *found->second.terrain->floor_world_height;
}

// Integer 3D distance without the original table. The original uses the
// 004BA410 quantized table; TODO-ORIGINAL for the exact fallback values.
int fallback_distance(Coord from, Coord to) {
    const auto dx = std::int64_t(from.x) - to.x, dy = std::int64_t(from.y) - to.y,
               dz = std::int64_t(from.z) - to.z;
    const auto squared = dx * dx + dy * dy + dz * dz;
    if (squared > std::numeric_limits<int>::max())
        throw std::range_error("prototype blast distance overflow");
    return static_cast<int>(std::sqrt(static_cast<double>(squared)));
}

int blast_distance(const OriginalDistance* table, Coord from, Coord to) {
    if (table) return table->between(from, to);
    return fallback_distance(from, to);
}

// Warhead armor list order, from the original verse lookup (005D3D20).
constexpr std::array<const char*, 11> armor_names{{"none", "flak", "plate", "light", "medium",
    "heavy", "wood", "steel", "concrete", "special_1", "special_2"}};

double warhead_verse(const IniFile& rules, const std::string& warhead, const Entity& target) {
    const auto armor = lower(rules.read_string(target.type, "Armor"));
    std::size_t index = armor_names.size();
    for (std::size_t i = 0; i < armor_names.size(); ++i)
        if (armor == armor_names[i]) { index = i; break; }
    const auto verses = IniFile::split_list(rules.read_string(warhead, "Verses"));
    // TODO-ORIGINAL: unknown armor or verse uses the original default verse.
    if (index >= armor_names.size() || index >= verses.size()) return 1.0;
    return IniFile::double_value(verses[index]).value_or(1.0);
}

// One weapon detonation for area damage.
struct BlastWeapon {
    std::string warhead;
    int damage = 0, max_damage = 10000;
    float spread = 0, percent = 1;
};

std::optional<BlastWeapon> warhead_weapon(const IniFile& rules, const std::string& warhead,
                                          int damage) {
    if (damage <= 0 || warhead.empty() || !find_section(rules, warhead)) return {};
    BlastWeapon out;
    out.warhead = warhead;
    out.damage = damage;
    // TODO-ORIGINAL: WarheadTypeClass constructor defaults for missing keys.
    out.spread = static_cast<float>(rules.read_double(warhead, "CellSpread", 0.0));
    out.percent = static_cast<float>(rules.read_double(warhead, "PercentAtMax", 1.0));
    out.max_damage = rules.read_integer("CombatDamage", "MaxDamage", 10000);
    if (out.spread < 0 || out.percent < 0 || out.max_damage <= 0) return {};
    return out;
}

std::optional<BlastWeapon> blast_weapon(const IniFile& rules, const std::string& section) {
    if (!find_section(rules, section)) return {};
    const auto damage = rules.read_integer(section, "Damage", 0);
    const auto warhead = rules.read_string(section, "Warhead");
    return warhead_weapon(rules, warhead, damage);
}

// Remove a dead object from every known Cell chain, preserving survivor order.
// TODO-ORIGINAL: Unit Mark(0), Cell setup and deferred deletion are separate
// original steps; this only keeps the shared chains consistent.
bool unlink_entity(Snapshot& world, Entity& victim) {
    if (!victim.next) return false;
    const auto id = victim.id;
    const auto following = *victim.next;
    std::map<std::uint32_t, Entity*> objects;
    for (auto& entity : world.entities)
        if (entity.id && !objects.emplace(entity.id, &entity).second) return false;
    bool unlinked = false;
    std::vector<w::CellState*> emptied;
    for (auto& pair : world.cells) {
        auto& cell = pair.second;
        if (!cell.heads) continue;
        for (auto* head : {&cell.heads->ground_head, &cell.heads->bridge_head}) {
            std::uint32_t previous = 0;
            auto link = *head;
            while (link) {
                const auto found = objects.find(link);
                if (found == objects.end() || !found->second->next) return false;
                if (link == id) {
                    if (previous) objects.at(previous)->next = following;
                    else *head = following;
                    if (head == &cell.heads->ground_head && !previous && !following)
                        emptied.push_back(&cell);
                    unlinked = true;
                    break;
                }
                previous = link;
                link = *found->second->next;
            }
        }
    }
    if (!unlinked) return false;
    for (auto* cell : emptied)
        if (cell->ground_occupancy_124) cell->ground_occupancy_124 = 0;
    return true;
}

struct BlastResult {
    int damaged = 0, destroyed = 0;
    std::vector<std::string> missing;
};

// Detonate one weapon at a position over every alive object in the blast
// radius. TODO-ORIGINAL: the original collects candidates per Cell in chain
// order with its own eligibility rules; this prototype sorts by distance.
BlastResult detonate(Snapshot& world, Coord epicenter, const BlastWeapon& weapon,
                     const IniFile& rules, const OriginalDistance* distance,
                     const std::map<std::uint32_t, std::uint64_t>& invulnerable,
                     std::uint64_t frame) {
    BlastResult result;
    const auto radius = static_cast<int>(weapon.spread * 256.0f);
    struct Candidate { std::uint32_t id; int distance; };
    std::vector<Candidate> candidates;
    for (const auto& entity : world.entities) {
        if (!alive(&entity)) continue;
        const auto shielded = invulnerable.find(entity.id);
        if (shielded != invulnerable.end() && shielded->second > frame) continue;
        try {
            const auto reach = blast_distance(distance, epicenter, *entity.position);
            if (reach <= radius) candidates.push_back({entity.id, reach});
        } catch (const std::exception& error) {
            result.missing.push_back(error.what());
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.id < b.id;
    });
    for (const auto& candidate : candidates) {
        auto* target = find_entity(world, candidate.id);
        if (!alive(target)) continue;
        int amount = 0;
        try {
            const auto verse = warhead_verse(rules, weapon.warhead, *target);
            amount = total_damage({weapon.damage, candidate.distance, weapon.max_damage,
                weapon.spread, weapon.percent, verse, false, true});
        } catch (const std::exception& error) {
            result.missing.push_back(error.what());
            continue;
        }
        if (amount <= 0) continue;
        // TODO-ORIGINAL: damage states, triggers, death visuals, scores and
        // the derived Unit/Techno handlers are separate original steps.
        const auto health = *target->resolved_hp - amount;
        ++result.damaged;
        if (health <= 0) {
            if (unlink_entity(world, *target)) {
                ++result.destroyed;
                target->resolved_hp = 0;
                target->active = false;
                target->in_limbo = true;
                target->target_id = 0;
                target->lifetime = w::Lifetime::Destroyed;
            } else {
                result.missing.push_back("dead object has unresolved Cell links");
                target->resolved_hp = 1;
            }
        } else {
            target->resolved_hp = health;
        }
    }
    return result;
}

// Move one placed object to another Cell: chains, occupancy and position.
// TODO-ORIGINAL: the original warp keeps subcells, layers and missions.
std::string teleport_entity(Snapshot& world, std::uint32_t id, ra2map::Cell destination) {
    auto* entity = find_entity(world, id);
    if (!entity || !entity->next || !entity->cell || !entity->position || !entity->layer)
        return "warped object state is unresolved";
    const auto source_key = cell_key(*entity->cell);
    const auto target_key = cell_key(destination);
    const auto landing_cell = world.cells.find(target_key);
    if (landing_cell == world.cells.end() || !landing_cell->second.heads ||
        !landing_cell->second.ground_occupancy_124)
        return "warp destination Cell is unresolved";
    // Unlink from the source chain.
    bool removed = false;
    for (auto& pair : world.cells) {
        auto& cell = pair.second;
        if (!cell.heads) continue;
        for (auto* head : {&cell.heads->ground_head, &cell.heads->bridge_head}) {
            std::uint32_t previous = 0;
            auto link = *head;
            while (link) {
                const auto* object = find_entity(world, link);
                if (!object || !object->next) return "Cell chain is unresolved";
                if (link == id) {
                    if (previous) {
                        auto* previous_object = find_entity(world, previous);
                        if (!previous_object) return "Cell chain is unresolved";
                        previous_object->next = *object->next;
                    } else {
                        *head = *object->next;
                    }
                    removed = true;
                    break;
                }
                previous = link;
                link = *object->next;
            }
        }
    }
    if (!removed) return "warped object is missing from its Cell chain";
    // Relink at the destination head.
    entity->next = landing_cell->second.heads->ground_head;
    landing_cell->second.heads->ground_head = id;
    const auto source = world.cells.find(source_key);
    if (source != world.cells.end() && source->second.heads &&
        source->second.heads->ground_head == 0 && source->second.ground_occupancy_124)
        source->second.ground_occupancy_124 = 0;
    *landing_cell->second.ground_occupancy_124 |= 0x20u;
    entity->cell = destination;
    const Coord position{destination.x * 256 + 128, destination.y * 256 + 128,
        floor_z(world, destination)};
    entity->position = position;
    entity->collision_virtual_position = position;
    return {};
}

// Spawn one parachuting infantry type on a free Cell.
bool drop_infantry(Snapshot& world, std::uint32_t house, const std::string& type,
                   ra2map::Cell cell, const IniFile& rules, std::uint32_t next_id,
                   std::vector<std::string>& missing) {
    const auto strength = rules.read_integer(type, "Strength", 0);
    if (strength <= 0) {
        missing.push_back("paratrooper Strength is unresolved: " + type);
        return false;
    }
    const auto found = world.cells.find(cell_key(cell));
    if (found == world.cells.end() || !found->second.allocated || !*found->second.allocated ||
        !found->second.heads || !found->second.ground_occupancy_124) {
        missing.push_back("drop Cell is unavailable");
        return false;
    }
    auto& cell_state = found->second;
    if (cell_state.heads->ground_head != 0 || *cell_state.ground_occupancy_124) return false;
    Entity infantry;
    infantry.id = next_id;
    infantry.house_id = house;
    infantry.type = type;
    infantry.kind = w::Kind::Infantry;
    infantry.lifetime = w::Lifetime::Placed;
    infantry.cell = cell;
    infantry.position = cell_center(cell);
    infantry.resolved_hp = strength;
    infantry.maximum_hp = strength;
    infantry.next = cell_state.heads->ground_head;
    infantry.active = true;
    infantry.in_limbo = false;
    infantry.on_map = true;
    infantry.layer = w::Layer::Ground;
    cell_state.heads->ground_head = infantry.id;
    *cell_state.ground_occupancy_124 |= 0x20u;
    world.entities.push_back(std::move(infantry));
    world.creation_order.push_back({w::CreatedKind::Entity, next_id});
    return true;
}

// Cells of one drop wave: the target first, then rings by Chebyshev distance.
// TODO-ORIGINAL: the original drops along the transport plane flight line.
std::vector<ra2map::Cell> drop_cells(ra2map::Cell target, std::size_t wanted) {
    std::vector<ra2map::Cell> cells;
    if (!in_map(target)) return cells;
    cells.push_back(target);
    for (int radius = 1; cells.size() < wanted && radius <= 8; ++radius)
        for (int dy = -radius; dy <= radius && cells.size() < wanted; ++dy)
            for (int dx = -radius; dx <= radius && cells.size() < wanted; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                const int x = int(target.x) + dx, y = int(target.y) + dy;
                if (x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                cells.push_back({static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
            }
    return cells;
}

} // namespace

SuperKind superweapon_kind(std::string_view type_name) {
    // Type= name table, in base rules order (see read_superweapon_types; the
    // base 1.006 Type parse handlers live around 006B and 0069, e.g. the
    // launch dispatch FUN_0069D850).
    static const std::array<std::pair<SuperKind, const char*>, 12> names{{
        {SuperKind::Nuke, "MultiMissile"}, {SuperKind::IronCurtain, "IronCurtain"},
        {SuperKind::LightningStorm, "LightningStorm"}, {SuperKind::ChronoSphere, "ChronoSphere"},
        {SuperKind::ChronoWarp, "ChronoWarp"}, {SuperKind::ParaDrop, "ParaDrop"},
        {SuperKind::AmerParaDrop, "AmerParaDrop"},
        {SuperKind::PsychicDominator, "PsychicDominator"}, {SuperKind::SpyPlane, "SpyPlane"},
        {SuperKind::GeneticMutator, "GeneticMutator"}, {SuperKind::ForceShield, "ForceShield"},
        {SuperKind::PsychicReveal, "PsychicReveal"},
    }};
    for (const auto& entry : names)
        if (same_id(type_name, entry.second)) return entry.first;
    return SuperKind::Invalid;
}

std::vector<SuperweaponType> read_superweapon_types(const IniFile& rules) {
    std::vector<SuperweaponType> out;
    const auto* registry = rules.find_section("SuperWeaponTypes");
    if (!registry) return out;
    for (const auto& entry : registry->entries) {
        const auto id = IniFile::trim(entry.value);
        if (id.empty()) continue;
        SuperweaponType type;
        type.id = std::string(id);
        if (find_section(rules, type.id)) {
            type.kind = superweapon_kind(rules.read_string(type.id, "Type"));
            // RechargeTime is minutes; the engine stores frames at 15 fps.
            // TODO-ORIGINAL: confirm the exact original rounding (006CExxx).
            const auto minutes = rules.read_double(type.id, "RechargeTime", 5.0);
            type.recharge_frames = static_cast<int>(minutes * 900.0);
            type.is_powered = rules.read_bool(type.id, "IsPowered", true);
            type.range_cells = static_cast<float>(rules.read_double(type.id, "Range", 0.0));
            type.line_multiplier = rules.read_integer(type.id, "LineMultiplier", 0);
            type.weapon = rules.read_string(type.id, "WeaponType");
            type.pre_dependent = rules.read_string(type.id, "PreDependent");
            type.pre_click = rules.read_bool(type.id, "PreClick");
            type.post_click = rules.read_bool(type.id, "PostClick");
            type.show_timer = rules.read_bool(type.id, "ShowTimer");
        }
        out.push_back(std::move(type));
    }
    return out;
}

std::map<std::string, std::vector<std::string>> superweapon_grants(const IniFile& rules) {
    std::map<std::string, std::vector<std::string>> out;
    const auto* registry = rules.find_section("BuildingTypes");
    if (!registry) return out;
    for (const auto& entry : registry->entries) {
        const auto building = std::string(IniFile::trim(entry.value));
        if (building.empty()) continue;
        for (const auto* key : {"SuperWeapon", "SuperWeapon2"}) {
            const std::string raw = rules.read_string(building, key);
            const std::string section(IniFile::trim(raw));
            if (!section.empty()) out[section].push_back(building);
        }
    }
    return out;
}

PrototypeSuperweapons::PrototypeSuperweapons(const IniFile& base_rules,
                                             const std::string& game_exe)
    : rules_(base_rules) {
    types_ = read_superweapon_types(rules_);
    const auto grants = superweapon_grants(rules_);
    grants_.resize(types_.size());
    for (std::size_t slot = 0; slot < types_.size(); ++slot) {
        index_[lower(types_[slot].id)] = slot;
        const auto found = grants.find(types_[slot].id);
        if (found != grants.end()) grants_[slot] = found->second;
        else
            for (const auto& pair : grants)
                if (same_id(pair.first, types_[slot].id)) { grants_[slot] = pair.second; break; }
    }
    if (!game_exe.empty()) {
        try {
            distance_ = std::make_unique<OriginalDistance>(OriginalDistance::from_executable(game_exe));
        } catch (const std::exception&) {
            distance_.reset(); // Fallback distance.
        }
    }
}

PrototypeSuperweapons::~PrototypeSuperweapons() = default;

const SuperweaponType* PrototypeSuperweapons::find_type(std::string_view section) const {
    const auto found = index_.find(lower(section));
    return found == index_.end() ? nullptr : &types_[found->second];
}

SuperweaponState* PrototypeSuperweapons::house_state(std::uint32_t house, const std::string& id) {
    return &houses_[house][id];
}

void PrototypeSuperweapons::grant(std::uint32_t house, std::size_t slot) {
    auto& state = houses_[house][types_[slot].id];
    state.present = true;
    state.ready = false;
    state.one_time = false;
    state.suspended = false;
    state.remaining = types_[slot].recharge_frames;
}

void PrototypeSuperweapons::lose(std::uint32_t house, std::size_t slot) {
    auto& state = houses_[house][types_[slot].id];
    state.present = false;
    state.ready = false;
    state.suspended = false;
    state.remaining = 0;
    // Losing the sphere cancels a stale arm; the warp must not fire through it.
    if (types_[slot].kind == SuperKind::ChronoSphere) armed_.erase(house);
}

void PrototypeSuperweapons::discharge(std::uint32_t house, std::size_t slot) {
    auto& state = houses_[house][types_[slot].id];
    state.ready = false;
    state.suspended = false;
    if (state.one_time) {
        state.present = false;
        state.remaining = 0;
    } else {
        state.remaining = types_[slot].recharge_frames;
    }
}

// Original low power test from HouseClass::UpdatePower (00508C30): a drain
// above the output suspends powered super weapons.
bool PrototypeSuperweapons::low_power(const w::House& house) const {
    const auto drain = house.power_drain.value_or(0);
    const auto output = house.power_output.value_or(0);
    return drain != 0 && output < drain;
}

void PrototypeSuperweapons::update_grants(const session::world::Snapshot& world) {
    for (const auto& house : world.houses) {
        if (!house.id) continue;
        for (std::size_t slot = 0; slot < types_.size(); ++slot) {
            bool owned = false;
            for (const auto& building : grants_[slot]) {
                for (const auto& entity : world.entities) {
                    if (entity.house_id != house.id || entity.kind != w::Kind::Structure ||
                        !same_id(entity.type, building))
                        continue;
                    const bool standing = entity.lifetime == w::Lifetime::Placed &&
                        (!entity.resolved_hp || *entity.resolved_hp > 0);
                    if (standing) { owned = true; break; }
                }
                if (owned) break;
            }
            auto& state = houses_[house.id][types_[slot].id];
            if (owned && !state.present) grant(house.id, slot);
            else if (!owned && state.present) lose(house.id, slot);
        }
    }
}

SuperTickReport PrototypeSuperweapons::tick(session::world::Snapshot& world, std::uint64_t frame) {
    SuperTickReport report;
    if (last_frame_ && frame <= *last_frame_) return report; // Repeated or rewound frame.
    last_frame_ = frame;
    for (auto& pair : houses_) {
        const auto house = std::find_if(world.houses.begin(), world.houses.end(),
            [&pair](const w::House& candidate) { return candidate.id == pair.first; });
        const bool powered_down = house != world.houses.end() && low_power(*house);
        for (auto& item : pair.second) {
            const auto slot = index_.find(lower(item.first));
            if (slot == index_.end()) continue;
            auto& state = item.second;
            if (!state.present) continue;
            // SetOnHold (base 1.006 export 0069CC60): low power pauses the
            // clock, power resumes it. RA1 SUPER.CPP Suspend agrees.
            if (types_[slot->second].is_powered && powered_down && !state.one_time)
                state.suspended = true;
            else if (state.suspended)
                state.suspended = false;
            if (!state.ready && !state.suspended && state.remaining > 0) {
                --state.remaining;
                if (state.remaining == 0) state.ready = true;
            }
        }
    }
    // Nukes in flight land when their missiles arrive (FUN_0069D850 case 0
    // spawns the bullet; the blast follows on a later frame). Every launch
    // keeps its own pending impact; none is lost.
    for (const auto& pending : nukes_) {
        if (frame < pending.impact) continue;
        auto weapon = blast_weapon(rules_, "NukePayload");
        if (!weapon) {
            // The hardcoded payload first; a WeaponType= override second.
            for (const auto& type : types_)
                if (type.kind == SuperKind::Nuke && !type.weapon.empty()) {
                    weapon = blast_weapon(rules_, type.weapon);
                    if (weapon) break;
                }
        }
        if (!weapon) {
            report.notes.push_back("nuke payload weapon is unavailable");
            continue;
        }
        auto epicenter = cell_center(pending.target);
        epicenter.z = floor_z(world, pending.target);
        const auto blast = detonate(world, epicenter, *weapon, rules_, distance_.get(),
            invulnerable_, frame);
        report.damaged += blast.damaged;
        report.destroyed += blast.destroyed;
        ++report.nukes;
        for (const auto& missing : blast.missing) report.notes.push_back(missing);
    }
    nukes_.erase(std::remove_if(nukes_.begin(), nukes_.end(),
        [frame](const PendingNuke& pending) { return frame >= pending.impact; }),
        nukes_.end());
    // Lightning storm strikes, one game frame at a time.
    if (!storm_ || !storm_->active) return report;
    // FUN_0051F930 stores the deferment; FUN_00520200 counts it down.
    const auto deferment = rules_.read_integer("General", "LightningDeferment", 250);
    const auto duration = rules_.read_integer("General", "LightningStormDuration", 180);
    const auto hit_delay = rules_.read_integer("General", "LightningHitDelay", 10);
    const auto scatter_delay = rules_.read_integer("General", "LightningScatterDelay", 5);
    const auto spread = rules_.read_integer("General", "LightningCellSpread", 10);
    const auto separation = rules_.read_integer("General", "LightningSeparation", 3);
    const auto damage = rules_.read_integer("General", "LightningDamage", 250);
    const auto weapon = warhead_weapon(rules_, rules_.read_string("General", "LightningWarhead"),
        damage);
    if (!weapon) return report;
    const auto striking = storm_->start + static_cast<std::uint64_t>(deferment);
    if (frame >= striking + static_cast<std::uint64_t>(duration)) {
        storm_->active = false;
        return report;
    }
    if (frame < striking) return report;
    // Base 1.006 storm state and strike path: FUN_0051F930 starts the storm
    // (DAT_00A51934 active, DAT_00A51878 target cell, DAT_00A51938 deferment
    // countdown) and FUN_00520200 advances it per frame: a bolt on the target
    // every LightningHitDelay (rules +0x13F4) and a scattered bolt every
    // LightningScatterDelay (+0x13F8) inside LightningCellSpread (+0x13FC),
    // keeping LightningSeparation (+0x1400) from the bolt list, via
    // FUN_0051FC90. The scatter draw keeps this prototype's own RNG pattern.
    const auto bolt = [&](ra2map::Cell cell) {
        if (!in_map(cell)) return;
        const auto result = detonate(world, cell_center(cell), *weapon, rules_, distance_.get(),
            invulnerable_, frame);
        report.damaged += result.damaged;
        report.destroyed += result.destroyed;
        for (const auto& missing : result.missing) report.notes.push_back(missing);
        bolts_.push_back(cell);
        if (bolts_.size() > 32) bolts_.erase(bolts_.begin());
    };
    if (hit_delay > 0 && frame % static_cast<std::uint64_t>(hit_delay) == 0) bolt(storm_->target);
    if (scatter_delay > 0 && frame % static_cast<std::uint64_t>(scatter_delay) == 0) {
        Random rng(world.random);
        const auto half = spread / 2;
        for (int attempt = 0; attempt < 3; ++attempt) {
            // The original draws both offsets, then checks the separation.
            const auto dx = rng.ranged(-half, half);
            const auto dy = rng.ranged(-half, half);
            const auto x = int(storm_->target.x) + dx;
            const auto y = int(storm_->target.y) + dy;
            if (x < 0 || y < 0 || x >= 512 || y >= 512) break;
            const ra2map::Cell candidate{static_cast<std::uint16_t>(x),
                static_cast<std::uint16_t>(y)};
            bool too_close = false;
            for (const auto& previous : bolts_) {
                const auto city = std::abs(int(previous.x) - x) + std::abs(int(previous.y) - y);
                if (city < separation) { too_close = true; break; }
            }
            if (too_close) break;
            bolt(candidate);
        }
        world.random = rng.state();
    }
    return report;
}

const SuperweaponState* PrototypeSuperweapons::state(std::uint32_t house,
                                                     std::string_view section) const {
    const auto found = index_.find(lower(section));
    if (found == index_.end()) return nullptr;
    const auto per_house = houses_.find(house);
    if (per_house == houses_.end()) return nullptr;
    const auto current = per_house->second.find(types_[found->second].id);
    return current == per_house->second.end() ? nullptr : &current->second;
}

bool PrototypeSuperweapons::ready(std::uint32_t house, std::string_view section) const {
    const auto* current = state(house, section);
    return current && current->present && current->ready && !current->suspended;
}

int PrototypeSuperweapons::frames_left(std::uint32_t house, std::string_view section) const {
    const auto* current = state(house, section);
    if (!current || !current->present) return -1;
    return current->ready ? 0 : current->remaining;
}

bool PrototypeSuperweapons::invulnerable(std::uint32_t entity, std::uint64_t frame) const {
    const auto found = invulnerable_.find(entity);
    return found != invulnerable_.end() && found->second > frame;
}

SuperLaunchReport PrototypeSuperweapons::fire(session::world::Snapshot& world, std::uint32_t house,
    std::string_view section, ra2map::Cell target, std::uint64_t frame) {
    SuperLaunchReport report;
    const auto* type = find_type(section);
    if (!type) {
        report.notes.push_back("unknown super weapon section");
        return report;
    }
    if (!in_map(target)) {
        report.notes.push_back("target Cell is outside the map");
        return report;
    }
    // ChronoWarp fires through the armed ChronoSphere, not its own charge
    // (the PreClick/PostClick/PreDependent pair).
    if (type->kind == SuperKind::ChronoWarp) {
        const auto armed = armed_.find(house);
        if (armed == armed_.end()) {
            report.notes.push_back("no armed ChronoSphere for this house");
            return report;
        }
        // The warp fires through the source sphere: it must still be granted,
        // charged and not suspended at the warp click. A stale arm from a lost
        // or discharged sphere must not fire.
        const SuperweaponState* sphere = nullptr;
        bool sphere_found = false;
        for (std::size_t slot = 0; slot < types_.size(); ++slot)
            if (types_[slot].kind == SuperKind::ChronoSphere) {
                sphere = house_state(house, types_[slot].id);
                sphere_found = true;
            }
        if (!sphere_found || !sphere || !sphere->present || !sphere->ready ||
            sphere->suspended) {
            report.notes.push_back("the armed ChronoSphere is not granted and ready");
            armed_.erase(house);
            return report;
        }
        // The second click is the DESTINATION; the source block is the armed
        // ChronoSphere cell (base 1.006 FUN_0069D850 case 4 reads +0x4A as the
        // source block and the click as the landing area). This prototype uses
        // the 3x3 block around the source.
        const auto source = armed->second;
        const auto destination = target;
        std::vector<ra2map::Cell> pattern;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = int(source.x) + dx, y = int(source.y) + dy;
                if (x < 0 || y < 0 || x >= 512 || y >= 512) continue;
                pattern.push_back({static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)});
            }
        for (const auto& from : pattern) {
            const auto found = world.cells.find(cell_key(from));
            if (found == world.cells.end() || !found->second.heads) continue;
            std::vector<std::uint32_t> occupants;
            for (auto id = found->second.heads->ground_head; id;) {
                const auto* object = find_entity(world, id);
                if (!object || !object->next) break;
                occupants.push_back(id);
                id = *object->next;
            }
            const int dx = int(from.x) - int(source.x);
            const int dy = int(from.y) - int(source.y);
            const int landing_x = int(destination.x) + dx;
            const int landing_y = int(destination.y) + dy;
            if (landing_x < 0 || landing_y < 0 || landing_x >= 512 || landing_y >= 512) continue;
            const ra2map::Cell landing{static_cast<std::uint16_t>(landing_x),
                static_cast<std::uint16_t>(landing_y)};
            for (const auto id : occupants) {
                const auto* object = find_entity(world, id);
                if (!alive(object) ||
                    (object->kind != w::Kind::Unit && object->kind != w::Kind::Infantry))
                    continue;
                // TODO-ORIGINAL: the original shifts warped objects to free
                // Cells; this prototype leaves blocked arrivals in place.
                const auto landing_cell = world.cells.find(cell_key(landing));
                const bool free_cell = landing_cell != world.cells.end() &&
                    landing_cell->second.heads && landing_cell->second.ground_occupancy_124 &&
                    landing_cell->second.heads->ground_head == 0 &&
                    !*landing_cell->second.ground_occupancy_124;
                if (!free_cell) continue;
                if (const auto error = teleport_entity(world, id, landing); error.empty())
                    ++report.teleported;
                else
                    report.notes.push_back(error);
            }
        }
        if (!report.teleported) report.notes.push_back("no object could warp to a free Cell");
        // Discharge the armed ChronoSphere (SuperClass::Discharged).
        for (std::size_t slot = 0; slot < types_.size(); ++slot)
            if (types_[slot].kind == SuperKind::ChronoSphere) {
                auto* sphere_state = house_state(house, types_[slot].id);
                if (sphere_state && sphere_state->ready) discharge(house, slot);
            }
        armed_.erase(house);
        report.fired = true;
        return report;
    }
    const auto* current = state(house, type->id);
    if (!current || !current->present) {
        report.notes.push_back("super weapon is not granted to this house");
        return report;
    }
    if (current->suspended) {
        report.notes.push_back("super weapon is suspended by low power");
        return report;
    }
    if (!current->ready) {
        report.notes.push_back("super weapon is not charged");
        return report;
    }
    const auto slot = index_.at(lower(type->id));
    switch (type->kind) {
        case SuperKind::Nuke: {
            // Base 1.006 FUN_0069D850 case 0 (MultiMissile) does not detonate at
            // the click: it passes the weapon (offset +0xA4) into CreateBullet
            // 00463990 and spawns the missile above the target. The flight comes
            // from the base rules weapon chain: [NukeCarrier] Speed=100 with
            // projectile [GiantNukeUp] rising to DetonationAltitude=20000 (the
            // rules comment: it only creates the downward one over the target),
            // then [NukePayload] Speed=10 with projectile [GiantNukeDown] falling
            // from DetonationAltitude=30000. The blast lands when the missile
            // arrives, so the impact is deferred.
            const auto carrier = !type->weapon.empty() ? type->weapon : std::string("NukeCarrier");
            const auto rise_speed =
                rules_.read_integer(carrier, "Speed", 100);
            const auto rise_altitude = rules_.read_integer(
                rules_.read_string(carrier, "Projectile", "GiantNukeUp"), "DetonationAltitude", 20000);
            // Validate the payload before the charge is consumed, so a rules
            // mistake cannot cost a launch.
            auto weapon = blast_weapon(rules_, "NukePayload");
            if (!weapon && !type->weapon.empty()) weapon = blast_weapon(rules_, type->weapon);
            if (!weapon) {
                report.notes.push_back("nuke payload weapon is unavailable");
                return report;
            }
            const auto fall_speed = rules_.read_integer("NukePayload", "Speed", 10);
            const auto fall_altitude = rules_.read_integer(
                rules_.read_string("NukePayload", "Projectile", "GiantNukeDown"),
                "DetonationAltitude", 30000);
            // TODO-ORIGINAL: the vertical projectiles carry Acceleration=1; this
            // prototype uses the constant-speed read of the rules values.
            const auto delay = (rise_altitude + rise_speed - 1) / rise_speed +
                (fall_altitude + fall_speed - 1) / fall_speed;
            nukes_.push_back(PendingNuke{frame + static_cast<std::uint64_t>(delay), target});
            report.notes.push_back("nuke missile in flight");
            break;
        }
        case SuperKind::LightningStorm: {
            // One storm at a time, as the original global LightningStorm state.
            storm_ = Storm{true, frame, target};
            bolts_.clear();
            report.notes.push_back(
                "TODO-ORIGINAL: storm strikes land over the deferment and duration");
            break;
        }
        case SuperKind::IronCurtain: {
            // Base 1.006 FUN_006B26D0 loops exactly nine DAT_00A6F248 offsets (a
            // 3x3 block around the click) and calls every object's virtual +148
            // with the duration from rules +0xCA4. Duration comes from
            // [CombatDamage] IronCurtainDuration, read at FUN_00646C60. Ownership
            // and infantry eligibility live in the object handler; this
            // prototype keeps them here.
            const auto duration = rules_.read_integer("CombatDamage", "IronCurtainDuration", 750);
            for (auto& entity : world.entities) {
                if (!alive(&entity) || entity.house_id != house || !entity.cell) continue;
                const auto distance = std::max(std::abs(int(entity.cell->x) - int(target.x)),
                    std::abs(int(entity.cell->y) - int(target.y)));
                if (distance > 1) continue;
                if (entity.kind == w::Kind::Infantry) {
                    // TODO-ORIGINAL: the original Iron Curtain kills infantry.
                    if (unlink_entity(world, entity)) {
                        entity.resolved_hp = 0;
                        entity.active = false;
                        entity.in_limbo = true;
                        entity.target_id = 0;
                        entity.lifetime = w::Lifetime::Destroyed;
                        ++report.destroyed;
                    } else {
                        report.notes.push_back("curtained infantry has unresolved Cell links");
                    }
                    continue;
                }
                if (entity.kind != w::Kind::Unit && entity.kind != w::Kind::Structure) continue;
                auto& until = invulnerable_[entity.id];
                until = std::max(until, frame + static_cast<std::uint64_t>(duration));
                ++report.shielded;
            }
            break;
        }
        case SuperKind::ChronoSphere:
            // First click of the pair: this stores the SOURCE cell, the block of
            // objects to warp (base 1.006 FUN_0069D850 case 3 stores the clicked
            // coordinate at SuperClass+0x4A). The ChronoWarp click then gives the
            // destination (FUN_004E68D0 copies +0x4A from the PreDependent).
            armed_[house] = target;
            break;
        case SuperKind::ParaDrop:
        case SuperKind::AmerParaDrop: {
            // Types and counts from the rules lists.
            std::vector<std::string> types;
            std::vector<int> counts;
            if (type->kind == SuperKind::AmerParaDrop) {
                types = IniFile::split_list(rules_.read_string("General", "AmerParaDropInf"));
                for (const auto& item :
                    IniFile::split_list(rules_.read_string("General", "AmerParaDropNum")))
                    if (const auto count = IniFile::integer_value(item)) counts.push_back(*count);
            } else {
                // TODO-ORIGINAL: the generic ParaDrop count is engine data.
                const std::string raw = rules_.read_string("General", "PParatrooper");
                const std::string paratrooper(IniFile::trim(raw));
                if (!paratrooper.empty()) types.push_back(paratrooper);
                counts.push_back(6);
            }
            std::vector<std::string> wave;
            for (std::size_t i = 0; i < types.size() && i < counts.size(); ++i)
                for (int k = 0; k < std::max(counts[i], 0); ++k) wave.push_back(types[i]);
            if (wave.empty()) {
                report.notes.push_back("paratrooper rules are unavailable");
                return report;
            }
            const auto cells = drop_cells(target, wave.size());
            std::uint32_t next_id = 1;
            for (const auto& entity : world.entities)
                next_id = std::max(next_id, entity.id + 1);
            for (std::size_t i = 0; i < cells.size() && i < wave.size(); ++i)
                if (drop_infantry(world, house, wave[i], cells[i], rules_, next_id + i, report.notes))
                    ++report.dropped;
            report.notes.push_back("TODO-ORIGINAL: transport plane flight skipped");
            break;
        }
        case SuperKind::SpyPlane: {
            // The spy plane flies a line across the map through the target,
            // revealing a CameraRange band along the path.
            // TODO-ORIGINAL: original flight direction and reveal timing.
            const auto camera = rules_.read_integer("General", "CameraRange", 9);
            for (int dy = -camera; dy <= camera; ++dy) {
                const int y = int(target.y) + dy;
                if (y < 0 || y >= 512) continue;
                for (int x = 0; x < 512; ++x) {
                    if (report.revealed.size() >= 20000) {
                        report.notes.push_back("reveal list truncated");
                        break;
                    }
                    report.revealed.push_back({static_cast<std::uint16_t>(x),
                        static_cast<std::uint16_t>(y)});
                }
            }
            break;
        }
        default:
            report.notes.push_back("TODO-ORIGINAL: this super weapon has no prototype effect");
            return report;
    }
    // SuperClass::Discharged: restart the charge. The ChronoSphere arm stays
    // charged until the following ChronoWarp consumes it.
    if (type->kind != SuperKind::ChronoSphere) discharge(house, slot);
    report.fired = true;
    return report;
}

} // namespace ra2::sim
