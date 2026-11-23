#include "prototype_psychic_sensor.h"
#include "distance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ra2::sim {
namespace {
namespace w = session::world;
using rules::IniFile;
using w::Entity;
using w::Snapshot;

// Original Object state this prototype treats as alive, matching the shared
// combat and superweapon prototype guards.
bool alive(const w::Entity* entity) {
    return entity && entity->id &&
        (entity->kind == w::Kind::Unit || entity->kind == w::Kind::Infantry ||
         entity->kind == w::Kind::Structure || entity->kind == w::Kind::Aircraft) &&
        entity->lifetime == w::Lifetime::Placed && entity->position &&
        (!entity->resolved_hp || *entity->resolved_hp > 0) &&
        (!entity->active || *entity->active) && (!entity->in_limbo || !*entity->in_limbo);
}

const w::Entity* find_entity(const Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.entities.begin(), world.entities.end(),
        [id](const Entity& entity) { return entity.id == id; });
    return found == world.entities.end() ? nullptr : &*found;
}

const w::House* find_house(const Snapshot& world, std::uint32_t id) {
    const auto found = std::find_if(world.houses.begin(), world.houses.end(),
        [id](const w::House& house) { return house.id == id; });
    return found == world.houses.end() ? nullptr : &*found;
}

// Base 004E5540 as the Psychic Sensor scan uses it: FUN_004386FF loads the
// current viewer into ECX, pushes the enemy owner and calls 004E5540. Only
// the viewer mask at +5674 is tested, one bit per original house index; the
// reverse enemy mask must NOT suppress detection. The same original_index
// also counts as allied. Houses without the recovered fields are never
// allied, so an enemy stays detectable.
bool allied(const Snapshot& world, std::uint32_t viewer, std::uint32_t other) {
    if (viewer == other) return true;
    const auto* hv = find_house(world, viewer);
    const auto* ho = find_house(world, other);
    if (!hv || !ho) return false;
    if (hv->original_index && ho->original_index &&
        *hv->original_index == *ho->original_index)
        return true;
    if (!ho->original_index || *ho->original_index < 0 ||
        *ho->original_index >= 32)
        return false;
    return hv->alliance_mask_5674 &&
        (*hv->alliance_mask_5674 & (1u << *ho->original_index));
}

// Active-sensor gate: Building vtable 0079CBBC+30C -> 00450360. The powered
// branch queries the House power percentage and rejects below 1.0. The
// output/drain fields are the per-house evidence from the 004F2CC0 recount
// (HouseClass::UpdatePower); the fraction stands in for GetPowerPercentage.
bool house_powered(const Snapshot& world, std::uint32_t house_id) {
    const auto* house = find_house(world, house_id);
    if (!house || !house->power_output || !house->power_drain) return true;
    if (*house->power_drain <= 0) return true;
    const double percentage =
        static_cast<double>(*house->power_output) / *house->power_drain;
    return percentage >= 1.0;
}

// TODO-ORIGINAL: fallback for fixture tests only. The original FUN_0044DAA0
// uses the quantized 004BA410 distance table; this integer Euclidean
// approximation stands in when no game.exe was supplied.
int fallback_distance(Coord from, Coord to) {
    const auto dx = std::int64_t(from.x) - to.x, dy = std::int64_t(from.y) - to.y,
               dz = std::int64_t(from.z) - to.z;
    const auto squared = dx * dx + dy * dy + dz * dz;
    if (squared > std::numeric_limits<int>::max())
        throw std::range_error("prototype psychic sensor distance overflow");
    return static_cast<int>(std::sqrt(static_cast<double>(squared)));
}
} // namespace

PrototypePsychicSensor::PrototypePsychicSensor(const IniFile& base_rules,
                                               const std::string& game_exe)
    : rules_(base_rules) { // Own a copy of the rules, matching core ownership.
    // Fallback distance only when no executable was supplied. A supplied but
    // invalid game.exe is a caller error and must report failure.
    if (!game_exe.empty())
        distance_ = std::make_unique<OriginalDistance>(
            OriginalDistance::from_executable(game_exe));
}

PrototypePsychicSensor::~PrototypePsychicSensor() = default;

std::vector<PsychicThreat> PrototypePsychicSensor::query(const w::Snapshot& world,
    std::uint32_t viewer_house, const PsychicGoalLookup& goal) const {
    // Collect the viewer's live sensors: placed active non-limbo structures
    // with a positive BuildingType PsychicDetectionRadius (+1390, written by
    // 0045A4DF..0045A4F2). Powered=yes sensors need house power.
    struct Sensor {
        std::uint32_t id;
        Coord position;
        int range_leptons; // radius * 256, FUN_0044DAA0.
    };
    std::vector<Sensor> sensors;
    for (const auto& entity : world.entities) {  // Only Powered=yes sensors care about power.
        if (entity.house_id != viewer_house || entity.kind != w::Kind::Structure ||
            !alive(&entity))
            continue;
        // Loader 0045A4E7 reads the radius with 0050D7F0, an integer read.
        const auto radius = rules_.read_integer(entity.type, "PsychicDetectionRadius", 0);
        if (radius <= 0.0) continue;
        if (rules_.read_bool(entity.type, "Powered", false) &&
            !house_powered(world, viewer_house))
            continue;
        sensors.push_back({entity.id, *entity.position,
            static_cast<int>(radius * 256.0)});
    }
    if (sensors.empty()) return {};

    // Enemies: living placed entities of non-allied houses.
    std::vector<const w::Entity*> enemies;
    for (const auto& entity : world.entities)
        if (alive(&entity) && !allied(world, viewer_house, entity.house_id))
            enemies.push_back(&entity);
    std::sort(enemies.begin(), enemies.end(),
        [](const w::Entity* a, const w::Entity* b) { return a->id < b->id; });

    const auto inside = [&](const Sensor& sensor, Coord destination) {
        const int distance = distance_ ? distance_->between(sensor.position, destination)
                                       : fallback_distance(sensor.position, destination);
        return distance <= sensor.range_leptons;
    };

    std::vector<PsychicThreat> threats;
    for (const auto* enemy : enemies) {
        // FUN_004386F0 order: the attack target (Techno +228) first, then the
        // movement destination / final route cell. Both are destination
        // checks; the enemy current position alone never triggers.
        std::optional<Coord> attack_destination;
        if (enemy->target_id && *enemy->target_id) {
            const auto* target = find_entity(world, *enemy->target_id);
            if (alive(target)) attack_destination = target->position;
        }
        std::optional<Coord> goal_destination;
        if (goal) goal_destination = goal(enemy->id);

        const Sensor* sensor = nullptr;
        bool attack = false;
        std::optional<Coord> destination;
        if (attack_destination) {
            for (const auto& candidate : sensors)
                if (inside(candidate, *attack_destination) &&
                    (!sensor || candidate.id < sensor->id)) {
                    sensor = &candidate;
                    attack = true;
                    destination = attack_destination;
                }
        }
        if (!sensor && goal_destination) {
            for (const auto& candidate : sensors)
                if (inside(candidate, *goal_destination) &&
                    (!sensor || candidate.id < sensor->id)) {
                    sensor = &candidate;
                    destination = goal_destination;
                }
        }
        if (!sensor) continue;
        threats.push_back({enemy->id, sensor->id, *enemy->position, *destination, attack});
    }
    return threats;
}

} // namespace ra2::sim
