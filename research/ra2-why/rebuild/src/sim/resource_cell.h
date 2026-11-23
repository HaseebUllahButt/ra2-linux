#pragma once

#include "sim.h" // RandomState: all 250 words and both indexes.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {
// Inputs to the base game's OverlayType lookup at 005DC0B0.
// For -1 the flag is not read. Otherwise it is OverlayType +279.
struct ResourceOverlay {
    std::optional<std::int32_t> index; // Cell +44.
    std::optional<bool> tiberium;
};
struct ResourceType {
    std::optional<std::int32_t> index; // TiberiumType +98; returned by lookup.
    std::optional<std::int32_t> first_overlay; // OverlayType at +E0, then +264.
    std::optional<std::int32_t> count; // +E8.
    std::optional<std::int32_t> extra_count; // +EC.
    std::optional<std::int32_t> value; // +B8.
    std::optional<std::int32_t> divisor; // +E4.
    // Raw original address at +E0. Needed only when randomize is true.
    std::optional<std::uint32_t> first_overlay_address;
};
struct ResourceCellInput {
    ResourceOverlay cell;
    std::optional<std::uint8_t> data; // Cell +11E before this call.
    // N, NE, E, SE, S, SW, W, NW. For a missing map Cell, supply
    // the original fallback Cell +44 and its resolved OverlayType flag.
    std::array<ResourceOverlay,8> neighbors;
    // Whether each resolved neighbor is the shared fallback Cell.
    std::array<std::optional<bool>,8> neighbor_fallback;
    std::optional<std::uint32_t> cell_coordinate; // Packed signed 16-bit x,y at Cell +24.
    std::optional<std::uint32_t> fallback_coordinate; // Fallback Cell +24 before this call.
    // Original registry order. A present empty vector is distinct from unknown.
    std::optional<std::vector<ResourceType>> registry;
    std::optional<bool> randomize; // Original byte argument; ordinary load uses false.
    std::optional<RandomState> random;
    std::optional<bool> random_disabled; // Generator +0, needed only for a draw.
};
enum class ResourceCellStatus { Complete, NeedsInput, Unsupported };
struct ResourceFallbackWrite {
    unsigned neighbor;
    std::uint32_t coordinate; // Packed signed 16-bit x,y.
};
struct ResourceCellResult {
    ResourceCellStatus status=ResourceCellStatus::NeedsInput;
    std::string detail;
    std::optional<std::int32_t> overlay;
    std::optional<std::uint8_t> data;
    std::optional<std::int32_t> value; // Original return value, when complete.
    std::optional<RandomState> random;
    std::optional<std::uint32_t> fallback_coordinate;
    std::vector<ResourceFallbackWrite> fallback_writes; // Original lookup order.
    bool overlay_written=false;
    bool data_written=false;
};
// Run 00476BA0, including 005DC0B0, its ordered neighbor scan, and
// 006388A0. Partial writes remain visible if later inputs are missing.
// Invalid original memory access, zero divisor, and nonterminating signed
// random ranges return Unsupported instead of inventing original behavior.
ResourceCellResult setup_original_resource_cell(const ResourceCellInput&);
}
