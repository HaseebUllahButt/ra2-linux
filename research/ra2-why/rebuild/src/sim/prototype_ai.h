#pragma once

#include "skirmish_order.h"
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace ra2::sim {

// TODO-ORIGINAL: A small native opponent, not the recovered Team/Trigger AI.
// One instance serves one match. Only authored rules and decision clocks are
// stored here. The caller owns and executes every order in the shared world.
class PrototypeAI {
public:
    explicit PrototypeAI(const rules::IniFile& base_rules);

    static constexpr std::int32_t build_interval_frames = 90;
    static constexpr std::int32_t attack_interval_frames = 45;

    // Deploy, then Build, then Attack in ascending actor ID order. Build orders
    // identify a live factory and use its cell as the production/placement anchor.
    // The runtime must reject duplicate requests while a factory is busy.
    // Repeated frames do nothing. A frame rewind or house change resets clocks.
    std::vector<skirmish::Order> tick(const session::world::Snapshot&,
        std::uint32_t computer_house_id, std::uint32_t player_house_id,
        std::int32_t game_frame);
    void reset() noexcept;

private:
    rules::IniFile rules_;
    std::optional<std::int32_t> last_frame_;
    std::uint32_t computer_house_ = 0, player_house_ = 0;
    std::int64_t next_build_ = 0, next_attack_ = 0;
};

} // namespace ra2::sim
