#pragma once
// Sidebar super weapon wiring for the native battle. Part 2 of the playable
// super weapon integration. The charge clocks, low power suspension and the
// launch effects live in sim/prototype_superweapon.h; this header only names,
// gates and routes the player facing parts.
//
// Original references (analysis/ghidra-dump/functions.csv):
// - 006cb560 SuperClass::Grant, 006cb7b0 Lose, 006cb4d0 SetOnHold,
//   006cc080 recharge tick;
// - 006cbca0 HasChargeProgressed, which triggers the ready announcement;
// - 006cb920 ClickFire and 00752700 VoxClass::PlayFromName for the speech.
#include "../sim/prototype_superweapon.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ra2::app {

// One sidebar super weapon row: label, charge clock and ready flag.
struct SuperweaponEntry {
    std::string section;     // [SuperWeaponTypes] section id, the fire key.
    std::string name;        // Sidebar label.
    bool ready = false;      // Charged, granted and not suspended.
    bool suspended = false;  // Held by low power.
    int frames_left = -1;    // -1: not granted. 0: ready.
    int recharge_frames = 0; // Full charge length, for the clock line.
};

// Click flow after a sidebar pick. ChronoSphere needs a source and a
// destination click (PreDependent ChronoWarp pair); every other weapon
// needs one target click.
enum class SuperweaponFlow { None, ChooseTarget, ChronoSource, ChronoDestination };

// The player's entries in [SuperWeaponTypes] order. Blocked sections (the
// Super Weapons option removed them) and not granted sections are left out.
// label resolves the sidebar name of a rules type.
inline std::vector<SuperweaponEntry> superweapon_entries(
    const std::vector<sim::SuperweaponType>& types,
    const sim::PrototypeSuperweapons& superweapons, std::uint32_t house,
    const std::set<std::string>& blocked,
    const std::function<std::string(const std::string&)>& label) {
    std::vector<SuperweaponEntry> entries;
    for (const auto& type : types) {
        if (blocked.count(type.id))continue;
        const int left = superweapons.frames_left(house, type.id);
        if (left < 0)continue;
        SuperweaponEntry entry;
        entry.section = type.id;
        entry.name = label(type.id);
        entry.ready = superweapons.ready(house, type.id);
        const auto* state = superweapons.state(house, type.id);
        entry.suspended = state && state->suspended;
        entry.frames_left = left;
        entry.recharge_frames = type.recharge_frames;
        entries.push_back(std::move(entry));
    }
    return entries;
}

// Sections the Super Weapons option (00A3D2C3) removes: every granting
// building has DisableableFromShell=yes, as House::Can_Build 004E3832
// refuses those buildings. disableable(building) answers the flag.
inline std::set<std::string> superweapons_blocked_by_option(
    const rules::IniFile& rules,
    const std::vector<sim::SuperweaponType>& types,
    const std::function<bool(const std::string& building)>& disableable) {
    std::set<std::string> blocked;
    const auto grants = sim::superweapon_grants(rules);
    for (const auto& type : types) {
        if (type.kind == sim::SuperKind::Invalid)continue;
        bool switchable = false;
        for (const auto& building : grants.at(type.id))
            if (!disableable(building)) { switchable = false; break; }
            else switchable = true;
        if (switchable)blocked.insert(type.id);
    }
    return blocked;
}

// EVA.INI speech for a charged weapon and for a launch warning. Unknown
// names are dropped by the audio mixer, so a missing line stays silent.
inline std::string superweapon_ready_speech(sim::SuperKind kind) {
    switch (kind) {
    case sim::SuperKind::Nuke: return "EVA_NuclearMissileReady";
    case sim::SuperKind::LightningStorm: return "EVA_WeatherControlDeviceReady";
    case sim::SuperKind::IronCurtain: return "EVA_IronCurtainReady";
    case sim::SuperKind::ChronoSphere: return "EVA_ChronosphereReady";
    case sim::SuperKind::PsychicDominator: return "EVA_PsychicDominatorReady";
    default: return {};
    }
}
inline std::string superweapon_launch_speech(sim::SuperKind kind) {
    switch (kind) {
    case sim::SuperKind::Nuke: return "EVA_NuclearMissileLaunchDetected";
    case sim::SuperKind::LightningStorm: return "EVA_WeatherControlDeviceLaunchDetected";
    default: return {};
    }
}

// Battlefield overlay panel, top left under the status line. One row per
// granted weapon. The draw code lives in battle.cpp; this is the layout.
struct SuperweaponPanel {
    static constexpr int x = 4, y = 24, width = 176, row_height = 15;
    static int height(std::size_t rows) {
        return rows ? int(rows) * row_height + 4 : 0;
    }
    // Row index of a click inside the panel, else nullopt.
    static std::optional<std::size_t> row_at(int px, int py, std::size_t rows) {
        if (px < x || px >= x + width || py < y) return {};
        const std::size_t row = std::size_t(py - y) / row_height;
        if (row >= rows || py >= y + height(rows)) return {};
        return row;
    }
};

} // namespace ra2::app
