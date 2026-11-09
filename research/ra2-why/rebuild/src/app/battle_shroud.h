#pragma once
// Base RA2 shroud for the playable skirmish. One object per match.
// Root calls update() once per game frame, then uses the queries and masks
// for the viewing house. The rules live in sim/prototype_shroud.h.
//
// Base skirmish has no fog of war (RevealFogShroud 00499FC0 runs CleanFog
// only with the scenario FogOfWar flag). So an explored cell shows its
// terrain and its objects; only black cells hide them. set_fog_of_war(true)
// switches objects to current sight only.
#include "../render/battle_radar.h"
#include "../render/tmp_decode.h"
#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include "../sim/prototype_shroud.h"
#include "../sim/sim.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ra2::app {

class BattleShroud {
public:
    // The grid matches the Snapshot cell keys (y*512+x).
    static constexpr std::int32_t grid_size = 512;

    // All cells an entity covers. Return an empty list to use entity.cell.
    // Root can wire sim::production::original_foundation_cells here.
    using Footprint =
        std::function<std::vector<ra2map::Cell>(const session::world::Entity&)>;
    // Frame pixel of a cell's floor centre, or no value when not drawn.
    struct FramePoint { int x = 0, y = 0; };
    using CellAnchor = std::function<std::optional<FramePoint>(ra2map::Cell)>;

    BattleShroud();

    void set_footprint(Footprint footprint) { footprint_ = std::move(footprint); }
    void set_fog_of_war(bool on) noexcept { fog_of_war_ = on; }
    bool fog_of_war() const noexcept { return fog_of_war_; }

    // Recompute sight for every house. Call once per game frame, after the
    // world step and before drawing. Buildings see from their footprint centre.
    void update(const session::world::Snapshot& world, const rules::IniFile& rules);

    // Terrain shows: the house has explored the cell and no enemy gap covers it.
    bool revealed(std::uint32_t house, ra2map::Cell cell) const;
    // A sight source of the house sees the cell now.
    bool visible_now(std::uint32_t house, ra2map::Cell cell) const;
    // Objects in the cell show to the house (revealed, or visible_now with fog).
    bool objects_shown(std::uint32_t house, ra2map::Cell cell) const;
    // The same test for a world coordinate (leptons, 256 per cell). Use it to
    // wrap BattleEffects::Project: return no point for a hidden coordinate.
    // BattleEffects drops an effect when either end has no point, so hidden
    // attackers lose their flash and tracer, and hidden targets their hit.
    bool coord_shown(std::uint32_t house, sim::Coord coord) const;

    // The house may see, select, hover and target this entity. Own entities
    // always show. Others show when any footprint cell shows its objects.
    bool entity_visible(std::uint32_t house, const session::world::Entity& entity) const;
    // A ready predicate for BattleView::visible and similar filters.
    std::function<bool(const session::world::Entity&)> visible_filter(std::uint32_t house) const;

    // Drop hidden entities from a drawing state (ids match the world).
    void hide_entities(sim::Snapshot& drawing, const session::world::Snapshot& world,
                       std::uint32_t house) const;

    // Black out unexplored cells in the battle frame, with a soft edge.
    // map_anchor gives each cell's floor centre in map pixels (camera
    // independent). The frame shows map pixel (camera_x + x, camera_y + y).
    // Only pixels inside clip change. Cells come from the world's allocated
    // cells. Floor anchors are read once and cached: call reset_mask_cache()
    // when the map or the anchor function changes. The dark map is rebuilt
    // only when the camera, clip, house or explored cells change; otherwise
    // the cached dark runs are applied again.
    void mask_scene(render::Bitmap& frame, const session::world::Snapshot& world,
                    std::uint32_t house, const CellAnchor& map_anchor,
                    std::int64_t camera_x, std::int64_t camera_y,
                    render::Rectangle clip) const;
    void reset_mask_cache() const;
    // How many times mask_scene rebuilt its dark map (for tests and timing).
    std::uint64_t mask_rebuilds() const noexcept { return mask_.rebuilds; }

    // Black out unexplored cells on a radar image from draw_battle_radar.
    // Uses the same flat projection as the radar, so enemy dots on black
    // cells go black too.
    void mask_radar(render::Bitmap& radar, const render::RadarView& view,
                    std::uint32_t house) const;

    // Darkness 0 (clear) to 4 (black) at the top corner of a cell: the
    // number of black cells among the four that share that corner.
    int corner_darkness(std::uint32_t house, ra2map::Cell cell) const;

    const sim::PrototypeShroud& rules_view() const noexcept { return shroud_; }

private:
    std::vector<ra2map::Cell> cells_of(const session::world::Entity& entity) const;
    bool revealed_xy(std::uint32_t house, std::int32_t x, std::int32_t y) const;

    sim::PrototypeShroud shroud_;
    // drop: pixels from the cell's floor diamond down to the floor of the
    // lowest cell in front of it (zero on flat ground). Cliff faces and high
    // ground leave this strip between the two diamonds.
    struct MaskCell { std::int32_t x, y; std::int64_t ax, ay; int drop = 0; };
    struct MaskRun { int y, x0, x1; bool black; }; // x1 excluded.
    struct MaskCache {
        bool anchors_ready = false;
        std::size_t cell_count = 0;
        std::vector<MaskCell> cells; // Sorted by anchor y.
        int max_drop = 0;
        std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // Cell box plus one cell.
        bool ready = false;
        std::int64_t camera_x = 0, camera_y = 0;
        int width = 0, height = 0;
        render::Rectangle clip;
        std::uint32_t house = 0;
        std::vector<std::uint8_t> layer;  // Explored layer of the last rebuild.
        std::vector<std::uint8_t> dark;   // Clip pixels, 0 clear .. 255 black.
        std::vector<MaskRun> runs;
        std::uint64_t rebuilds = 0;
    };
    void rebuild_mask(std::uint32_t house, std::int64_t camera_x, std::int64_t camera_y,
                      render::Rectangle clip) const;
    mutable std::vector<std::uint8_t> layer_; // Scratch for mask_scene.
    mutable MaskCache mask_;
    Footprint footprint_;
    bool fog_of_war_ = false;
};

} // namespace ra2::app
