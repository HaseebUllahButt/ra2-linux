#pragma once
// Prototype win and lose detection for the first playable native Linux
// skirmish. It reads one session world snapshot and never changes it.
// The defeat rule is a simplification; see the TODO-ORIGINAL note in
// prototype_victory.cpp.
#include "../session/world/world.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ra2::sim {
// One verdict per snapshot. The app shows the first settled verdict.
enum class Victory { InProgress, PlayerWon, PlayerLost, Draw };

// Latches the first non-InProgress verdict. The world keeps running after
// game over, so later snapshots must not change a settled verdict.
struct VictoryState {
    Victory result=Victory::InProgress;
    bool settled() const noexcept { return result!=Victory::InProgress; }
    // Feed every new snapshot. Returns the verdict to display.
    Victory update(const session::world::Snapshot& world,
        std::uint32_t player_house, std::uint32_t enemy_house);
    // The same latch with the original rule.
    Victory update(const session::world::Snapshot& world,
        std::uint32_t player_house, std::uint32_t enemy_house, const struct VictoryRules& rules);
    // The original rule with several enemy houses. A fallen house stays
    // fallen (House +13D). The player wins when every enemy has fallen and
    // loses when the player house falls. Call it once per game frame.
    Victory update(const session::world::Snapshot& world, std::uint32_t player_house,
        const std::vector<std::uint32_t>& enemy_houses, const struct VictoryRules& rules);
    bool fallen(std::uint32_t house) const noexcept;
    std::vector<std::uint32_t> fallen_houses; // In the order they fell.
};

// Original automatic defeat rule, base game.exe HouseClass::AI 004E4110
// (the block that calls 004E7DF0 and 004E77F0). It runs for each multiplayer
// house that is not passive and not yet defeated, after frame 0.
struct VictoryRules {
    // 00A3D2C2, the Short Game setup option. On: a house falls when it owns
    // no building and none of the first two [General] BaseUnit types.
    // Off: a house falls when it owns no building, aircraft, infantry or unit.
    bool short_game=true;
    // [General] BaseUnit. The original reads entries 0 and 1 only.
    std::vector<std::string> base_units;
};
// True when the original rule defeats this house in this snapshot.
bool original_house_fallen(const session::world::Snapshot& world, std::uint32_t house,
    const VictoryRules& rules);
// Verdict from the original rule. Only objects on the map count (Placed).
// TODO-ORIGINAL: 004E7DF0 then destroys every object of the fallen house, and
// Harvester Truce (scenario flag 0x800) removes HarvesterUnit types from the
// unit count. Both are not modeled. Throws like evaluate_prototype_victory.
Victory evaluate_original_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house, const VictoryRules& rules);

// Pure verdict for one snapshot, from the simplified prototype defeat rule.
// Counts surviving Units, Infantry and Structures with positive health that
// belong to the two given houses. Created and Placed objects count; Destroyed
// objects and map-neutral houses never count. Unknown health stays alive.
// Throws std::invalid_argument when a house id is zero, when both ids match,
// when a house is missing from the snapshot, or when a given house has the
// map-neutral or special role.
Victory evaluate_prototype_victory(const session::world::Snapshot& world,
    std::uint32_t player_house, std::uint32_t enemy_house);
}
