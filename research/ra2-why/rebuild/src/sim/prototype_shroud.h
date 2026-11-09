#pragma once
// Shroud for the native Linux skirmish. Every house owns one shroud map over
// one shared rectangular cell grid. A cell starts black. A live unit or
// building with Sight= reveals the cells in its sight area; a revealed cell
// stays explored. A gap generator hides its radius from enemy houses. A
// SpySat=yes building reveals the whole map; losing it reshrouds the map.
//
// Base RA2 evidence (game.exe 1.006, base addresses):
// - RevealArea 0054A110 caps the range at ten cells and walks the CellSpread
//   offset table 00A6F248 (built by 005441E0, counts 007A5908:
//   1,9,21,37,61,89,121,161,205,253,309). It also drops cells whose
//   truncated Euclid distance exceeds the range, which never removes a table
//   cell. The table is exactly the cells with
//   max(|dx|,|dy|) + min(|dx|,|dy|)/2 <= range (checked for ranges 0..10).
// - TechnoTypeClass 006D9B40 sets Sight to zero and 006DAF60 (006DC9D9)
//   reads Sight= over it. A type without Sight= reveals nothing.
// - Techno sight 006D5C30 passes the object coordinate, so a building sees
//   from its footprint centre. Use tick()'s centre callback for that.
// - RevealFogShroud 00499FC0 runs CleanFog only with the scenario FogOfWar
//   flag. Base skirmish has no fog: an explored cell shows its objects.
#include "rules/ini_file.h"
#include "session/world/world.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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

    // The cell an entity sees from. Return no value to use entity.cell.
    using SightCenter =
        std::function<std::optional<ra2map::Cell>(const session::world::Entity&)>;

    // Recompute every house's view from one snapshot. Sight=, GapGenerator=,
    // GapRadiusInCells= and SpySat= come from the authored rules. Call once
    // per game frame. Explored cells stay explored; current sight is rebuilt.
    void tick(const session::world::Snapshot& world, const rules::IniFile& rules,
              const SightCenter& center = {});

    // Reveal the sight area around one cell for one house. Range zero or
    // less does nothing; RevealArea 0054A110 caps the range at ten cells.
    void sight_from(std::uint32_t house, ra2map::Cell center, std::int32_t range);

    // MapClass::Shroud_From: hide the sight diamond from every other known
    // house, without un-exploring it (gap generator behavior).
    void shroud_from(std::uint32_t house, ra2map::Cell center, std::int32_t range);

    // Spy satellite: explore and reveal the whole map for one house.
    void reveal_all(std::uint32_t house);

    // Forget the whole map for one house (spy satellite loss).
    void reshroud_all(std::uint32_t house);

    // Terrain draws when the cell is revealed, objects draw when it is
    // visible. Unknown houses and cells outside the grid read as Hidden.
    bool cell_revealed_to(std::uint32_t house, ra2map::Cell cell) const;
    bool cell_visible_to(std::uint32_t house, ra2map::Cell cell) const;
    Vision vision(std::uint32_t house, ra2map::Cell cell) const;

    // One byte per grid cell (index y*width+x): 1 when cell_revealed_to is
    // true. For fast drawing passes; unknown houses give all zeros.
    void revealed_layer(std::uint32_t house, std::vector<std::uint8_t>& out) const;

    // Sight= in cells for one type. A missing key reads as zero (006D9B40).
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
