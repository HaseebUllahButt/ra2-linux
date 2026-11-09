#pragma once
// Save slots for the options menu Load, Save and Delete buttons.
// Files use the original name pattern "SAVE%04lX.SAV" from game.exe.
// The content is native, not the original .SAV layout: a small header with
// the player's description, then the caller's payload, for example
// session::save::encode(game). TODO-ORIGINAL: original .SAV container.
// Limit: session::save stores the world, factories and legacy route records.
// It cannot restore fresh match controllers. Missing state includes AI teams,
// weights and clocks; movement steps and soldier spots; combat orders,
// projectiles and turret clocks; miner cargo and dock clocks; explored terrain;
// repair flags; queues; victory and score history; and map/setup identity.
// The save test keeps its movement controllers alive across the world reload.
// Keep Load and Save disabled until a fresh-controller replay test passes.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ra2::app {
struct SaveSlot {
    std::string file;        // SAVE0000.SAV style name, no folder.
    std::string description; // Text the player typed.
};
class BattleSaves {
public:
    explicit BattleSaves(std::filesystem::path folder);
    // Valid slots, sorted by file name. Damaged files are left out.
    std::vector<SaveSlot> list() const;
    bool empty() const { return list().empty(); }
    // Writes a new slot with the next free number; returns its name.
    std::optional<std::string> save(const std::string& description, const std::string& payload);
    // Replaces an existing or new named slot. Rejects names that are not SAVE####.SAV.
    bool save_as(const std::string& file, const std::string& description, const std::string& payload);
    std::optional<std::string> load(const std::string& file) const;
    bool remove(const std::string& file);
    static bool valid_name(const std::string& file);
private:
    std::filesystem::path folder_;
};
}
