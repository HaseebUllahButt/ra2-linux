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
    std::int32_t third_survivor_divisor = 0;  // [General] ThirdSurvivorDivisor; the base
                                              // RULES.INI omits it and the base parser
                                              // reads no third divisor. Zero means a
                                              // third-side crew is never spawned unless
                                              // the rules author a value.
    std::int32_t ticks_per_minute = 900;      // Original TICKS_PER_MINUTE.

    // Pass the loaded base RULES.INI, with any intended map overrides applied.
    // Throws std::runtime_error for missing or invalid required fields.
    static PrototypeRepairSellRules load(const rules::IniFile& rules);
};

// Player building repair and sell for prototype worlds. One instance serves
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

    // Credits for one repair step of the building type. The base step path
    // in FUN_0044B9B0 reads the per-step cost from the type virtual at
    // vtable +0xB0 and the per-step health from +0xB4; those virtual bodies
    // are not recovered, so this keeps the RA1 shape (TechnoTypeClass::
    // Repair_Cost): Cost / (Strength / RepairStep) times RepairPercent,
    // truncated. Zero when the type has no usable Cost or Strength, which
    // repairs for free like an original truncated zero.
    std::int32_t repair_cost_per_step(const std::string& type) const;

    // Credits paid back for selling one of the type: Cost * RefundPercent,
    // truncated. Base game.exe AircraftClass::GetRefund 0x006D5850 calls the
    // same type virtual at vtable +0xB8; its body was not recovered, so this
    // keeps the RA1 TechnoClass::Refund_Amount shape. Zero without Cost.
    std::int32_t refund_amount(const std::string& type) const;

    // Crew size when selling. TODO-ORIGINAL: no deterministic base sell
    // count is proven. FUN_0043F8E0 is the random destruction-survivor
    // spray (random gate, one roll per foundation cell); it reads a count
    // from the BuildingClass virtual at vtable 0x0079CBBC slot 172
    // (0x0044C520, body not in the export) and the crew type from GetCrew
    // (slot 184, 0x00449FB0, which has a 25 percent engineer branch). This
    // keeps the RA1 divisor shape with the parsed [General] divisors as a
    // stand-in: refund divided by the side divisor, clamped to 1..5. Side 0
    // is Allied, 1 Soviet, 2 third. Zero when the type is not Crewed, the
    // side is unknown, or the refund is zero. TODO-ORIGINAL: the captured-
    // building divisor doubling is not modeled.
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
