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

    // The house may see, select, hover and target this entity. Own entities
    // always show. Others show when any footprint cell shows its objects.
    bool entity_visible(std::uint32_t house, const session::world::Entity& entity) const;
    // A ready predicate for BattleView::visible and similar filters.
    std::function<bool(const session::world::Entity&)> visible_filter(std::uint32_t house) const;

    // Drop hidden entities from a drawing state (ids match the world).
    void hide_entities(sim::Snapshot& drawing, const session::world::Snapshot& world,
                       std::uint32_t house) const;

    // Black out unexplored cells in the battle frame, with a soft edge.
    // anchor gives each cell's floor centre in frame pixels. Only pixels
    // inside clip change. Cells come from the world's allocated cells.
    void mask_scene(render::Bitmap& frame, const session::world::Snapshot& world,
                    std::uint32_t house, const CellAnchor& anchor,
                    render::Rectangle clip) const;

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
    mutable std::vector<std::uint8_t> layer_; // Scratch for mask_scene.
    Footprint footprint_;
    bool fog_of_war_ = false;
};

} // namespace ra2::app
