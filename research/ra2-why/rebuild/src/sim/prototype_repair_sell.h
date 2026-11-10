#pragma once
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {
// Authored [General] repair and sell values for the first playable skirmish.
// The INI reader turns percent strings such as "15%" into shares (0.15).
struct PrototypeRepairSellRules {
    double repair_percent = 0.0;      // [General] RepairPercent: full repair cost as a share of Cost.
    double repair_rate_minutes = 0.0; // [General] RepairRate: minutes between repair steps.
    std::int32_t repair_step = 0;     // [General] RepairStep: health per repair step.
    double refund_percent = 0.0;      // [General] RefundPercent: sell refund as a share of Cost.
    std::int32_t allied_survivor_divisor = 0; // [General] AlliedSurvivorDivisor.
    std::int32_t soviet_survivor_divisor = 0; // [General] SovietSurvivorDivisor.
    // The base game has no third survivor divisor; only a side value of 1
    // uses the Soviet divisor, every other side value uses the Allied one.
    std::int32_t ticks_per_minute = 900;      // Original TICKS_PER_MINUTE.

    // Pass the loaded base RULES.INI, with any intended map overrides applied.
    // Throws std::runtime_error for missing or invalid required fields.
    static PrototypeRepairSellRules load(const rules::IniFile& rules);
};

// Human-player building repair and sell for prototype worlds. One instance serves
// one match. Repair state lives here, not in the world; a rebuilt helper
// forgets which buildings were repairing, like the movement prototype.
// TODO-ORIGINAL: no deconstruction animation or timer (RA2 BuildingClass::
// Mission_Selling, RA1 Mission_Deconstruction BUILDING.CPP 3385), no sounds,
// no repair-bay selling of docked units. The base auto-repair path
// FUN_0044B9B0 also holds computer repair decisions (it calls the owner
// virtuals at +0x188 and +0x184); those decisions are not modeled.
class PrototypeRepairSell {
public:
    // The art INI is optional; without it every footprint is one cell.
    explicit PrototypeRepairSell(const rules::IniFile& base_rules,
        const rules::IniFile* art_rules = nullptr);

    // Credits for one repair step of the building type. Recovered from
    // game.exe: TechnoTypeClass vtable 0x007AD080, slot +0xB0 = 0x006DAEC0
    // is Cost / (Strength / RepairStep) times RepairPercent, truncated,
    // with a minimum of 1. Slot +0xB4 = 0x006DAF10 returns RepairStep.
    // Zero when the type has no usable Cost or Strength (the original
    // would divide by zero instead).
    std::int32_t repair_cost_per_step(const std::string& type) const;

    // Credits paid back for selling one of the type: Cost * RefundPercent,
    // truncated. Recovered from game.exe: TechnoTypeClass vtable 0x007AD080
    // slot +0xB8 = 0x006DAD80; its extra arguments can force the full price
    // and apply a house factor, which are not modeled. Zero without Cost.
    std::int32_t refund_amount(const std::string& type) const;

    // Crew size when selling. Recovered from game.exe 0x0044C520
    // (BuildingClass vtable 0x0079CBBC slot 172): the refund (type virtual
    // +0xB8 with the owner and 0) divided by the side divisor, clamped to
    // 1..5, zero refund included. Only a side value of 1 picks the Soviet
    // divisor (RulesClass +0x1164); every other value picks the Allied one
    // (+0x1160). A zero divisor returns 0. TODO-ORIGINAL: the original
    // gates on BuildingClass +0x5B0 and House +0xAD9 and doubles the
    // divisor for a captured building (+0x5B3); this keeps the Crewed
    // rules check as its gate and does not model capture.
    std::int32_t survivor_count(const session::world::House& house, const std::string& type) const;

    // Infantry type of the sold building's crew. TODO-ORIGINAL: the base
    // GetCrew 0x00449FB0 can return an engineer (25 percent branch) and its
    // full selection is not recovered; the prototype always uses the side's
    // basic infantry (Allied E1, Soviet E2, third INIT).
    const char* crew_type(const session::world::House& house) const;

    // Start or stop repair on one placed, alive building. Returns whether the
    // requested state is now set. Repair never starts at full health.
    bool set_repair(session::world::Snapshot& world, std::uint32_t building_id, bool on);
    bool repairing(std::uint32_t building_id) const;

    struct SellResult {
        enum class Status { Complete, NeedsInput, Invalid };
        Status status = Status::Invalid;
        std::string detail;
        std::int32_t refund = 0;
        std::vector<std::uint32_t> crew; // Spawned infantry ids in spawn order.
    };
    // Sell one placed building now: pay the refund, free its foundation
    // cells, remove it and place its crew on free cells around it, nearest
    // first. A survivor without a free cell is dropped, as in the original
    // failed Unlimbo path. Needs a PrototypeComplete world.
    SellResult sell(session::world::Snapshot& world, std::uint32_t building_id);

    struct RepairTick {
        std::vector<std::uint32_t> repaired; // Buildings healed this tick.
        std::int32_t spent = 0;              // Credits charged this tick.
    };
    // Apply one repair step to every repairing building at the RepairRate
    // interval, original style: the shared frame must be a multiple of
    // RepairRate * TICKS_PER_MINUTE. Repeated or rewound frames do nothing.
    // A repair stops when the house cannot pay or the building is full.
    RepairTick tick(session::world::Snapshot& world, std::uint32_t game_frame);

private:
    rules::IniFile rules_;
    const rules::IniFile* art_ = nullptr;
    PrototypeRepairSellRules limits_;
    std::optional<std::uint32_t> last_frame_;
    std::vector<std::uint32_t> repairing_; // Building ids, ascending.
};

} // namespace ra2::sim
