#pragma once
#include "../session/world/world.h"
#include <functional>
#include <optional>
#include <string>
namespace ra2::sim {
enum class UnitMarkStatus { Complete, NeedsInput, Unsupported };
struct UnitMarkResult {
    UnitMarkStatus status=UnitMarkStatus::NeedsInput;
    bool value=false; // Original return value, only when Complete.
    std::string detail;
};
struct UnitMarkCallbacks {
    std::optional<int> bridge_height;
    // Resolved bodies in original call order. Return true only when the whole
    // named body completed. All callbacks operate on the private snapshot.
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t,int)> object_pre_mark;
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t)> refresh_after_mark;
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t)> techno_dirty;
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t,std::uint32_t)> insertion_visibility;
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t)> cell_setup;
};
// Base Foot 004C2E60, ordinary single-cell ground Unit. It preserves the
// Object +30 list order and the full occupancy word. Original external bodies
// are explicit; missing effects leave the caller's complete snapshot unchanged.
UnitMarkResult original_unit_mark(session::world::Snapshot&,std::uint32_t unit,int mode,const UnitMarkCallbacks&);
}
