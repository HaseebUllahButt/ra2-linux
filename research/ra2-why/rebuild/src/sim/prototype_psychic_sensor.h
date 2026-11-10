#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ra2::sim {

// One incoming order the Psychic Sensor reveals. `from` is the enemy current
// position, `to` is the detected destination: the attack target position when
// `attack` is true, else the caller supplied movement goal.
struct PsychicThreat {
    std::uint32_t enemy_id = 0;
    std::uint32_t sensor_id = 0;
    Coord from{};
    Coord to{};
    bool attack = false;
};

// Caller supplied movement destination for one enemy, Foot +34..3C or the
// final route cell (base FUN_004386F0). Empty means no known goal.
using PsychicGoalLookup = std::function<std::optional<Coord>(std::uint32_t enemy_id)>;

// Pure detection query, a rebuild of the base Psychic Sensor scan:
// FUN_004386F0 picks each enemy attack target (Techno +228) or Foot movement
// destination / final route cell and tests it against every active owned
// sensor; FUN_0044DAA0 accepts a destination inside the 3D range
// radius * 256 leptons of the sensor (BuildingType PsychicDetectionRadius,
// read into +1390 at 0045A4DF..0045A4F2; the SpySat reveal is the separate
// flag at +1345). The query never mutates the world, the random state or the
// shroud, and never reveals shroud itself.
class PrototypePsychicSensor {
public:
    // game_exe enables the original distance table for FUN_0044DAA0; an empty
    // path falls back to an integer Euclidean distance (see the .cpp note).
    explicit PrototypePsychicSensor(const rules::IniFile& base_rules,
        const std::string& game_exe = {});
    ~PrototypePsychicSensor(); // Out of line: holds the original distance table.

    // Threats for one viewer house, one per detected enemy, ordered by enemy
    // id. Sensors are living placed active non-limbo structures owned by the
    // viewer with a positive PsychicDetectionRadius; a Powered=yes sensor is
    // offline while the house power output is below its drain (House power
    // fields, base HouseClass::UpdatePower 004F2CC0). Enemies are living
    // placed entities of houses that are neither the viewer nor its allies
    // (alliance_mask_5674 with original_index).
    std::vector<PsychicThreat> query(const session::world::Snapshot& world,
        std::uint32_t viewer_house, const PsychicGoalLookup& goal) const;

private:
    const rules::IniFile& rules_;
    std::unique_ptr<OriginalDistance> distance_;
};

} // namespace ra2::sim
