#pragma once
#include "world/world.h"
#include "sim/production_runtime.h"

namespace ra2::session::save {
inline constexpr std::uint32_t format_version=2;
// Caller-owned routes. Preserve path order and the movement clock on restore.
struct PendingMove {
    ra2map::Cell goal{};
    std::int32_t speed=0;
    std::vector<ra2map::Cell> path;
};
struct SavedGame {
    std::uint64_t frame=0;
    world::Snapshot world;
    sim::production::FactorySet factories;
    std::map<std::uint32_t,PendingMove> moves;
    std::optional<std::uint32_t> movement_last_frame;
};
// Fixed-width little-endian format. TODO-ORIGINAL Save_Game: original .SAV layout.
std::string encode(const SavedGame&);
// Reject damaged input without changing out. No pointers or raw structs on disk.
bool decode(const std::string&,SavedGame& out);
bool save_file(const std::string& path,const SavedGame&);
std::optional<SavedGame> load_file(const std::string& path);
}
