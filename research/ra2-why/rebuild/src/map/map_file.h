// Red Alert 2 / Yuri's Revenge map file reader.
//
// Maps are INI text (verified against the 175 extracted maps). This
// layer parses every gameplay section into typed structures.
//
// Verified from the game's own code:
// - [Waypoints] values are cell coords packed as Y*1000 + X
//   (FUN_0068bdc0: value%1000 -> u16 X, value/1000 -> u16 Y).
// - [Header] carries StartX/StartY/Width/Height/NumberStartingPoints
//   plus "Waypoint%d=X,Y" pairs for each starting spot
//   (FUN_00689d30).
//
// Packed terrain and overlays are decoded by the native pack reader.
#pragma once

#include "../rules/ini_file.h"
#include "../pack/map_pack.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ra2map {

struct Cell {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

struct Structure {
    std::string house;
    std::string type;
    std::int32_t health = 256;  // 256 = 100%
    Cell cell;
    std::int32_t facing = 0;
    std::string tag;
};

struct Unit {
    std::string house;
    std::string type;
    std::int32_t health = 256;
    Cell cell;
    std::int32_t facing = 0;
    std::string mission;
};

struct Infantry {
    std::string house;
    std::string type;
    std::int32_t health = 256;
    Cell cell;
    std::int32_t subcell = 0;  // 0-4 position inside the cell
    std::int32_t facing = 0;
    std::string mission;
};

struct Aircraft {
    std::string house;
    std::string type;
    std::int32_t health = 256;
    Cell cell;
    std::int32_t facing = 0;
    std::string mission;
};

struct TerrainObject {
    Cell cell;
    std::string type;
};

struct Waypoint {
    std::int32_t index = 0;
    Cell cell;
};

class MapFile {
public:
    // Parses map text; throws std::runtime_error on I/O failure via IniFile.
    void parse(std::string_view text);

    // [Map]
    const std::string& theater() const noexcept { return theater_; }
    std::int32_t size_left() const noexcept { return size_left_;
    }
    std::int32_t size_top() const noexcept { return size_top_; }
    std::int32_t size_right() const noexcept { return size_right_; }
    std::int32_t size_bottom() const noexcept { return size_bottom_; }

    // [Basic]
    const std::string& next_scenario() const noexcept { return next_scenario_;
    }
    const std::string& alt_next_scenario() const noexcept { return alt_next_scenario_; }
    bool carry_over_cap() const noexcept { return carry_over_cap_; }

    const std::vector<Structure>& structures() const noexcept { return structures_; }
    const std::vector<Unit>& units() const noexcept { return units_; }
    const std::vector<Infantry>& infantry() const noexcept { return infantry_; }
    const std::vector<Aircraft>& aircraft() const noexcept { return aircraft_; }
    const std::vector<TerrainObject>& terrain() const noexcept { return terrain_; }
    const std::vector<Waypoint>& waypoints() const noexcept { return waypoints_; }

    const ra2::pack::Terrain& packed_terrain() const noexcept { return packed_terrain_; }

    // Original base64 text is also retained for inspection.
    const std::string& iso_map_pack5() const noexcept { return iso_pack_; }
    const std::string& overlay_pack() const noexcept { return overlay_pack_; }

    const ra2::rules::IniFile::Statistics& stats() const noexcept { return stats_; }

private:
    using Ini = ra2::rules::IniFile;

    std::string theater_ = "TEMPERATE";
    std::int32_t size_left_ = 0;
    std::int32_t size_top_ = 0;
    std::int32_t size_right_ = 0;
    std::int32_t size_bottom_ = 0;
    std::string next_scenario_;
    std::string alt_next_scenario_;
    bool carry_over_cap_ = false;
    std::vector<Structure> structures_;
    std::vector<Unit> units_;
    std::vector<Infantry> infantry_;
    std::vector<Aircraft> aircraft_;
    std::vector<TerrainObject> terrain_;
    std::vector<Waypoint> waypoints_;
    std::string iso_pack_;
    std::string overlay_pack_;
    ra2::pack::Terrain packed_terrain_;
    ra2::rules::IniFile::Statistics stats_;
};

} // namespace ra2map
