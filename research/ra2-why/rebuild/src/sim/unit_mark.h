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
    // Optional override for Object pre-mark. The built-in ordinary Unit path
    // uses the original read-only getters when this is absent.
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t,int)> object_pre_mark;
    // Base 004E09A0(0), reached only when Object +75 changes from zero to one.
    // Return true only when the complete body ran on the private Snapshot.
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
