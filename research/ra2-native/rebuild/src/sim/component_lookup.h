#pragma once
#include "movement_regions.h"
#include "../map/map_file.h"
#include <functional>
#include <optional>
#include <string>
namespace ra2::sim {
// Raw 16-byte records at Map +50 (base) / +54 (Yuri). Preserve order and unknown fields.
struct ComponentTransition {
    std::optional<ra2map::Cell> first,second;
    std::optional<std::uint8_t> field_8;
    std::optional<std::int32_t> field_c;
};
enum class ComponentLookupStatus { Complete,NeedsInput,Unsupported };
struct TransitionLookupResult {
    ComponentLookupStatus status=ComponentLookupStatus::NeedsInput;
    std::optional<int> index; // Complete -1 means no record. Missing input does not mean -1.
    std::string detail;
};
// Base 005500D0 / Yuri 0056DA10. Uses signed Cell words and the first admitted record.
// A nonvertical record uses first.y, not second.y. Do not normalize or reorder endpoints.
TransitionLookupResult original_find_component_transition(ra2map::Cell,int tolerance,int start,
    const std::vector<ComponentTransition>&);
struct ComponentCellState {
    std::optional<ra2map::Cell> object_cell; // Actual Cell +24, after original aliases or fallback lookup.
    std::optional<bool> bridge; // Explicit Cell +140 bit 100.
    std::optional<std::int32_t> tile_index,land_type; // Live Cell +38/+EC.
};
using ComponentCellLookup=std::function<std::optional<ComponentCellState>(ra2map::Cell)>;
struct ComponentLookupInputs {
    std::optional<std::vector<ComponentTransition>> transitions;
    // Raw BridgeSet/WoodBridgeSet first TileType indexes: base A52C74/A6CB68, Yuri AA0E28/ABAD1C.
    // Readers 00529870 / 00545150 resolve these globals. Yuri's Lunar suffix resets both to -1.
    std::optional<std::int32_t> first_tile_range,second_tile_range;
};
struct MovementComponentQuery {
    ra2map::Cell cell;
    unsigned movement_zone=0;
    bool above_bridge=false;
    // Recovery guard only. It is not an original engine limit. Exceeding it returns Unsupported.
    unsigned maximum_walk_steps=512u*512u;
};
enum class ComponentLookupStep { SourceCell,Transition,WalkCell,Neighbor,FirstTileRange,SecondTileRange,Ground };
struct MovementComponentResult {
    ComponentLookupStatus status=ComponentLookupStatus::NeedsInput;
    std::optional<std::uint32_t> component; // Keep FFFFFFFF bridge miss distinct from a ground FFFF word.
    std::optional<int> transition_index;
    std::optional<ra2map::Cell> selected_cell;
    unsigned walk_steps=0;
    std::vector<ComponentLookupStep> steps; // Diagnostic order. Only Complete can supply a live result.
    std::string detail;
};
// Base 0047B960/0047B980 omit Yuri 00486750/00486770's explicit start==-1 exclusion.
// Both editions preserve signed comparisons and 32-bit wrapping of start+16.
bool original_component_tile_range(DriveEdition,std::int32_t start,std::int32_t tile);
// Decision body of base 0054F8F0 / Yuri 0056D230, with transition selection and bridge endpoint choice.
// Neighbor offsets are proved by initializers 00491620 / 0049F2F0. Ground tables remain resolved inputs.
// Lookup must preserve actual Cell +24 and original fallback state. Callbacks must be read-only.
// Lookup supplies the fallback coordinate for each request. This API does not publish global fallback writes.
// No shared RNG is consumed. Missing values never become bridge=false, empty records, or blocked components.
MovementComponentResult original_movement_component(const CoarseRegions&,const MovementRegions&,unsigned stride,
    const MovementComponentQuery&,const ComponentLookupInputs&,const ComponentCellLookup&);
}
