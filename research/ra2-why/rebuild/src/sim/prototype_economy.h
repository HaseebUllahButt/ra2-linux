#pragma once
#include "prototype_movement.h"
#include "rules/ini_file.h"
namespace ra2::sim {
// One instance per match. Owns routes for miners only; tick once per frame.
// The battle loop must not give these miners to another movement instance.
// Construct from base rules.ini. Call with monotonically increasing frame IDs.
// Incomplete snapshots and duplicate/older frames do nothing.
// Cargo belongs to this instance; create a fresh instance for each match.
// Live placed CMIN/HARV units work automatically, without a harvest order.
class PrototypeEconomy {
public:
    explicit PrototypeEconomy(const rules::IniFile& rules);
    void tick(session::world::Snapshot& world, std::uint32_t frame);
    int load(std::uint32_t miner) const;
private:
    struct Cargo { int bails=0, credits=0; std::uint32_t next=0; bool returning=false; };
    struct Resource { std::string section; int first_overlay=-1; };
    std::vector<Resource> resources_;
    std::map<int,int> overlay_resources_;
    rules::IniFile rules_;
    PrototypeMovement movement_;
    std::map<std::uint32_t,Cargo> cargo_;
    std::optional<std::uint32_t> last_;
    int resource(const session::world::CellState&) const;
};
}
