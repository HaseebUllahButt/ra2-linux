#pragma once
#include "battle_hud.h"
#include <algorithm>
#include "../sim/prototype_repair_sell.h"

namespace ra2::app {
// Building commands share one owner check for clicks and queued execution.
class BattleBuildingTools {
public:
    BattleBuildingTools(const rules::IniFile& rules, const rules::IniFile& art)
        : rules_(rules), runtime_(rules, &art) {}
    bool allowed(const session::world::Entity& e, std::uint32_t player, BuildingTool tool) const {
        return tool != BuildingTool::None && e.house_id == player &&
            e.kind == session::world::Kind::Structure &&
            e.lifetime == session::world::Lifetime::Placed && e.resolved_hp.value_or(0) > 0 &&
            (tool == BuildingTool::Repair ? rules_.read_bool(e.type, "Repairable", true) :
                !rules_.read_bool(e.type, "Unsellable", false));
    }
    struct Result { bool changed = false, sold = false; std::string message; };
    Result apply(session::world::Snapshot& world, std::uint32_t player,
                 std::uint32_t id, BuildingTool tool) {
        const auto found = std::find_if(world.entities.begin(), world.entities.end(),
            [id](const auto& e) { return e.id == id; });
        if (found == world.entities.end() || !allowed(*found, player, tool))
            return {false, false, "Choose one of your buildings."};
        if (tool == BuildingTool::Repair) {
            const bool on = !runtime_.repairing(id);
            const bool changed = runtime_.set_repair(world, id, on);
            return {changed, false, changed ? (on ? "Repairing building." : "Repair stopped.") : "Building is at full health."};
        }
        const auto result = runtime_.sell(world, id);
        const bool sold = result.status == sim::PrototypeRepairSell::SellResult::Status::Complete;
        return {sold, sold, sold ? "Building sold." : "Cannot sell this building."};
    }
    sim::PrototypeRepairSell::RepairTick tick(session::world::Snapshot& world, std::uint32_t frame) {
        return runtime_.tick(world, frame);
    }
    bool repairing(std::uint32_t id) const { return runtime_.repairing(id); }
private:
    const rules::IniFile& rules_;
    sim::PrototypeRepairSell runtime_;
};
} // namespace ra2::app
