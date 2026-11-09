#pragma once
#include <cstdint>
#include <optional>
namespace ra2::sim {
struct EntryHeightCell {
    std::int8_t level=0;
    std::uint8_t ramp=0;
    bool flag_100=false,flag_200=false; // Separate original Cell +140 bits.
};
struct HeightEntryInputs {
    EntryHeightCell destination;
    std::optional<EntryHeightCell> source; // Required for directions other than -1.
    int direction=-1,layer_height=-1;
    bool above_bridge=false;
};
struct HeightEntryResult { int status,layer_height; bool above_bridge; };
// Base 004C8730 / expansion 004D9C60, Unit virtual height gate.
// Zero passes this gate only. It does not prove complete UnitClass cell eligibility.
HeightEntryResult original_height_entry(const HeightEntryInputs&);
}
