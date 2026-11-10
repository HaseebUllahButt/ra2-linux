#pragma once
// Sidebar super weapon wiring for the native battle. Part 2 of the playable
// super weapon integration. The charge clocks, low power suspension and the
// launch effects live in sim/prototype_superweapon.h; this header only names,
// gates and routes the player facing parts.
//
// Original references, confirmed in the base export
// (export/index.csv and export/functions.jsonl on the base dump):
// - 006be010 team script Iron Curtain handler at the team centre;
// - 006be1c0 team script ChronoSphere handler: source team centre,
//   destination enemy building through 006bcf70;
// - 006be440 destination Greatest_Threat handler.
// Base charge functions confirmed in the base RA2 1.006 export: SetOnHold
// 0069CC60, Lose 0069CF40 and the launch dispatch 0069D850 (see the sim
// header). Other SuperClass internals stay unverified against this dump.
#include "../sim/prototype_superweapon.h"
#include "battle_hud.h"
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

// One ordered fire call. ChronoSphere fires the sphere section at the
// source first, then the PreDependent ChronoWarp section at the
// destination (base 006be1c0 handler order: source team centre first).
struct SuperweaponFireStep {
    std::string section;   // [SuperWeaponTypes] section id to fire.
    ra2map::Cell cell;     // Fire target cell.
};

// The ordered fire plan for one completed click flow. A ChronoSphere pick
// without a source cell, or a rules set without a ChronoWarp section,
// returns an empty plan; the caller reports the gap.
inline std::vector<SuperweaponFireStep> superweapon_fire_plan(
    const std::vector<sim::SuperweaponType>& types, const std::string& section,
    const std::optional<ra2map::Cell>& source, ra2map::Cell target) {
    std::vector<SuperweaponFireStep> steps;
    const sim::SuperweaponType* picked = nullptr;
    const sim::SuperweaponType* warp = nullptr;
    for (const auto& type : types) {
        if (type.id == section)picked = &type;
        if (type.kind == sim::SuperKind::ChronoWarp)warp = &type;
    }
    if (!picked)return steps;
    if (picked->kind == sim::SuperKind::ChronoSphere) {
        if (!warp || !source)return steps;
        steps.push_back({section, *source});
        steps.push_back({warp->id, target});
    } else {
        steps.push_back({section, target});
    }
    return steps;
}
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
// building has DisableableFromShell=yes, as House::Can_Build refuses those
// buildings. disableable(building) answers the flag.
inline std::set<std::string> superweapons_blocked_by_option(
    const rules::IniFile& rules,
    const std::vector<sim::SuperweaponType>& types,
    const std::function<bool(const std::string& building)>& disableable) {
    std::set<std::string> blocked;
    const auto grants = sim::superweapon_grants(rules);
    for (const auto& type : types) {
        if (type.kind == sim::SuperKind::Invalid)continue;
        const auto listed = grants.find(type.id);
        if (listed == grants.end())continue;
        bool switchable = false;
        for (const auto& building : listed->second)
            if (!disableable(building)) { switchable = false; break; }
            else switchable = true;
        if (switchable)blocked.insert(type.id);
    }
    return blocked;
}

// EVA.INI speech, from the stock base EVA.INI sections. Unknown names are
// dropped by the audio mixer, so a missing line stays silent.
//
// A charged own weapon announces Ready. A fired weapon announces the
// launch line: the nuke was launched, a lightning storm was created.
// EVA_WeatherDeviceReady is the weather device DETECTED warning, not the
// charged line, so it belongs to the detected mapping only.
inline std::string superweapon_ready_speech(sim::SuperKind kind) {
    switch (kind) {
    case sim::SuperKind::Nuke: return "EVA_NuclearMissileReady";
    case sim::SuperKind::LightningStorm: return "EVA_LightningStormReady";
    case sim::SuperKind::IronCurtain: return "EVA_IronCurtainReady";
    case sim::SuperKind::ChronoSphere: return "EVA_ChronosphereReady";
    default: return {};
    }
}
inline std::string superweapon_launch_speech(sim::SuperKind kind) {
    switch (kind) {
    case sim::SuperKind::Nuke: return "EVA_NuclearMissileLaunched";
    case sim::SuperKind::LightningStorm: return "EVA_LightningStormCreated";
    // The activation lines fire when the player uses the weapon. The
    // ChronoWarp half of the pair stays silent, so one activation
    // announces once.
    case sim::SuperKind::IronCurtain: return "EVA_IronCurtainActivated";
    case sim::SuperKind::ChronoSphere: return "EVA_ChronosphereActivated";
    default: return {};
    }
}
// An enemy house received a weapon: the detected warning. Own and allied
// grants stay silent; the caller checks the house before speaking.
inline std::string superweapon_detected_speech(sim::SuperKind kind) {
    switch (kind) {
    case sim::SuperKind::Nuke: return "EVA_NuclearSiloDetected";
    case sim::SuperKind::LightningStorm: return "EVA_WeatherDeviceReady";
    case sim::SuperKind::IronCurtain: return "EVA_IronCurtainDetected";
    case sim::SuperKind::ChronoSphere: return "EVA_ChronosphereDetected";
    default: return {};
    }
}

// Every EVA.INI section the super weapon speech uses, for the audio
// preload. Verified stock base sections only.
inline std::vector<const char*> superweapon_speech_sections() {
    return {"EVA_NuclearSiloDetected", "EVA_NuclearMissileLaunched",
        "EVA_NuclearMissileReady", "EVA_IronCurtainDetected",
        "EVA_IronCurtainActivated", "EVA_IronCurtainReady",
        "EVA_ChronosphereDetected", "EVA_ChronosphereActivated",
        "EVA_ChronosphereReady", "EVA_WeatherDeviceReady",
        "EVA_LightningStormCreated", "EVA_LightningStormReady"};
}
// The sections the launch mapping names, for the activation tests.
inline std::vector<const char*> superweapon_activation_sections() {
    return {"EVA_IronCurtainActivated", "EVA_ChronosphereActivated"};
}

// Sidebar cameo entry for one granted weapon (battle_hud.h BuildChoice).
// The section id is the choice type, so the root can route the click. The
// charge clock shows as the production progress; only a ready weapon is
// enabled, and a low power hold shows as held.
inline BuildChoice superweapon_choice(const SuperweaponEntry& entry) {
    BuildChoice choice;
    choice.type = entry.section;
    choice.name = entry.name;
    choice.enabled = entry.ready;
    choice.progress = entry.ready || entry.recharge_frames <= 0 ? 1000 :
        1000 * (entry.recharge_frames - entry.frames_left) / entry.recharge_frames;
    choice.held = entry.suspended;
    choice.category = BuildCategory::Defenses;
    return choice;
}

// True when the type names a super weapon section, not a rules build type.
inline bool is_superweapon_pick(const std::vector<sim::SuperweaponType>& types,
                                const std::string& type) {
    for (const auto& item : types) if (item.id == type) return true;
    return false;
}

} // namespace ra2::app
