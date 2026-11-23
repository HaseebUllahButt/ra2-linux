#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {
// Overlay::Mark: base 005DA970, expansion 005FC570. This is an ordered
// dispatcher. It does not supply the called bodies or authorize live placement.
struct OverlayMarkType {
    std::optional<std::int32_t> index; // Base +264 / expansion +294.
    std::optional<std::int32_t> land; // +268 / +298.
    std::optional<bool> wall; // +278 / +2A8.
    std::optional<bool> field_27a; // +27A / +2AA.
    std::optional<bool> has_animation; // +26C / +29C != 0.
};
struct OverlayMarkCell {
    std::optional<std::uint8_t> ramp; // Cell +11C.
    std::optional<std::int32_t> overlay; // Cell +44; -1 is empty.
};
enum class OverlayRulesSlot { Other, FieldF8, FieldF4OrF0 };
struct OverlayMarkEnvironment {
    std::optional<bool> placement_override; // Base 00A40774 / expansion 00A8E7AC != 0.
    // Resolved identity against Rules +F8/+F4/+F0, expansion +100/+FC/+F8.
    std::optional<OverlayRulesSlot> rules_slot;
    std::optional<std::int32_t> import_token; // Base 007E8A8C / expansion 008333BC.
    std::optional<std::uint32_t> frame; // Base 00833688 / expansion 00880994.
};
enum class OverlayEffectKind {
    ParentMark, LocateCell, BridgeA, BridgeB, VeinholeFits, CreateVeinhole,
    CreateCrate, WallFits, UpdateWall, MarkZone, RebuildZones, Clearance,
    UpdateResource, CreateAnimation, TileSetup, FinishObject, Dispose,
    WriteOverlay, WriteData, WriteOwner50, IncrementNeighbor
};
struct OverlayEffect {
    OverlayEffectKind kind;
    std::int32_t value=0;
    std::int32_t second=0;
    std::int32_t third=0;
    int neighbor=-1; // -1 is the root; 0..7 uses the original Cell neighbor getter.
};
struct OverlayMarkCallbacks {
    // Return the current actor type and root Cell after preceding effects.
    std::function<std::optional<OverlayMarkType>()> type;
    std::function<std::optional<OverlayMarkCell>()> cell;
    // Existing OverlayType +282 / expansion +2B2.
    std::function<std::optional<bool>(std::int32_t)> existing_blocks;
    // Required for every reached call and write. Empty means unresolved.
    // Boolean results matter only for ParentMark, VeinholeFits, WallFits,
    // and Clearance. Other results are ignored, including false.
    std::function<std::optional<bool>(const OverlayEffect&)> effect;
};
enum class OverlayMarkStatus { Applied, Rejected, NeedsInput, Unsupported };
// Applied and Rejected preserve the original true/false result. Applied does
// not guarantee an overlay write: failed clearance can still return true.
struct OverlayMarkResult {
    OverlayMarkStatus status=OverlayMarkStatus::NeedsInput;
    std::string detail;
    std::vector<OverlayEffect> effects; // Includes the first unresolved effect.
};
// Callbacks can change state. An error does not undo preceding callbacks.
// Use a private staged world when a caller needs atomic publication.
// Bridge approach indexes 122..125 and 233..236 remain Unsupported.
// CreateAnimation delegates the complete original animation branch, not just allocation.
OverlayMarkResult dispatch_original_overlay_mark(int mode,
    const OverlayMarkEnvironment&,const OverlayMarkCallbacks&);
}
