#pragma once
#include <cstdint>
#include <optional>
#include <vector>
namespace ra2::sim {
// Field labels use expansion offsets. Base counterparts are listed below.
struct CoarseOverlayState {
    std::optional<bool> flag_22d,flag_2a8,flag_2b5,flag_2b4; // Base +1FD,+278,+285,+284.
    std::optional<float> land_wheel; // Wheel at base 00850EA8 / expansion 0089EA48, stride 36; OverlayType +268/+298 selects land.
};
struct CoarseOccupantState {
    std::optional<int> runtime_type; // Original virtual +2C; six=Building, 36=Terrain.
    std::optional<bool> building_flag_16c0,building_flag_16bf; // Base +135A,+1359 through Building +418.
    std::optional<bool> house_flag_1fa; // Base +142 through Building +1B4; expansion owner at +21C.
    std::optional<int> building_state_618; // Base Building +4EC.
    std::optional<int> terrain_field_2a8,terrain_field_2ac; // Base +278,+27C through Terrain +B0; expansion +C8.
};
struct CoarseCategoryInputs {
    std::optional<bool> usable;
    std::optional<int> overlay_index; // Minus one proves no overlay. Unknown is not empty.
    std::optional<CoarseOverlayState> overlay;
    std::optional<int> land_type;
    std::optional<float> land_wheel; // Original Wheel column, not Track and not this Unit's movement multiplier.
    std::optional<std::vector<CoarseOccupantState>> occupants; // Original ground linked-list order.
    std::optional<int> session_field_1258; // Base Session +838.
};
struct CoarseCategoryResult {
    std::uint8_t category=7;
    std::vector<std::uint8_t> writes; // Ordered writes to Cell +4C, including overwritten building writes.
};
// Base 00478F40 / expansion 00483C80. Resolves only fields reached by the original branches.
// This produces a coarse region category, not LandType or full movement eligibility.
CoarseCategoryResult original_coarse_category(const CoarseCategoryInputs&);
}
