#pragma once
// Prototype shroud and fog of war for the first playable native Linux
// skirmish. Every house owns one shroud map over one shared rectangular cell
// grid. A cell starts black. A live unit or building with Sight= reveals the
// cells in its sight diamond; a revealed cell stays explored, and objects in
// it draw only while some sight source sees the cell right now. A gap
// generator hides its radius from enemy houses. A SpySat=yes building
// reveals the whole map; losing it reshrouds the whole map again.
//
// The sight shape is the Red Alert sight diamond: a cell at offset (dx,dy) is
// in range when max(|dx|,|dy|) + min(|dx|,|dy|)/2 <= range. That is the shape
// of RA1 MapClass::Sight_From (ref/CnC_Red_Alert CODE/MAP.CPP,
// RadiusOffset/RadiusCount) and the RA2 engine keeps the same tables
// (RevealArea1 005673A0, RevealArea3 00567DA0 read the same offset list).
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ra2::sim {

class PrototypeShroud {
public:
    // What one house knows about one cell. Hidden = black shroud, Explored =
    // terrain drawn, Visible = objects drawn too. A gap-covered cell reads
    // Hidden while the gap lasts, then returns to its old state.
    enum class Vision : std::uint8_t { Hidden, Explored, Visible };

    // Grid size in cells; the caller owns the snapshot and the rules.
    PrototypeShroud(std::int32_t width, std::int32_t height);

    // Recompute every house's view from one snapshot. Sight=, GapGenerator=,
    // GapRadiusInCells= and SpySat= come from the authored rules. Call once
    // per game frame; repeated frames and rewinds are safe: the view is a
    // pure recompute, so the caller can replay snapshots in any order.
    void tick(const session::world::Snapshot& world, const rules::IniFile& rules);

    // MapClass::Sight_From: reveal the sight diamond around one cell for one
    // house. Range zero or less does nothing; the original display reveal
    // caps the range at ten cells (RevealArea1 005673A0).
    void sight_from(std::uint32_t house, ra2map::Cell center, std::int32_t range);

    // MapClass::Shroud_From: hide the sight diamond from every other known
    // house, without un-exploring it (gap generator behavior).
    void shroud_from(std::uint32_t house, ra2map::Cell center, std::int32_t range);

    // Spy satellite: explore and reveal the whole map for one house.
    void reveal_all(std::uint32_t house);

    // MapClass::Reshroud 00577AB0: forget the whole map for one house.
    void reshroud_all(std::uint32_t house);

    // Terrain draws when the cell is revealed, objects draw when it is
    // visible. Unknown houses and cells outside the grid read as Hidden.
    bool cell_revealed_to(std::uint32_t house, ra2map::Cell cell) const;
    bool cell_visible_to(std::uint32_t house, ra2map::Cell cell) const;
    Vision vision(std::uint32_t house, ra2map::Cell cell) const;

    // Sight= in cells for one type. A missing key reads as one cell.
    // TODO-ORIGINAL: the TechnoTypeClass Sight default is not recovered.
    std::int32_t sight_range(const rules::IniFile& rules, const std::string& type) const;

    // Grid size in cells, for callers that draw or query the map.
    std::int32_t width() const noexcept { return width_; }
    std::int32_t height() const noexcept { return height_; }

private:
    struct HouseMap {
        std::vector<std::uint8_t> revealed; // Explored terrain, monotonic.
        std::vector<std::uint8_t> visible;  // In current sight, recomputed.
        std::vector<std::uint8_t> gaps;     // Live gap generators covering the cell.
    };
    HouseMap& map_for(std::uint32_t house);
    const HouseMap* find_map(std::uint32_t house) const noexcept;
    bool in_grid(ra2map::Cell cell) const noexcept;
    std::size_t index_of(ra2map::Cell cell) const noexcept;

    std::int32_t width_ = 0, height_ = 0;
    std::map<std::uint32_t, HouseMap> houses_;
    std::map<std::uint32_t, bool> satellite_; // Last tick's spy satellite state.
};

} // namespace ra2::sim
