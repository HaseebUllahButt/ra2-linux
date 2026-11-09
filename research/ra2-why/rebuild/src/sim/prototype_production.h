#pragma once
#include "production_runtime.h"
#include <utility>

// TODO-ORIGINAL: A playable adapter around the checked production slices.
// The caller owns one PrototypeComplete Snapshot and one FactorySet.
// These functions never publish the private phase used by checked slices.
namespace ra2::sim::production::prototype {
Outcome initialize(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t player_house);
Outcome deploy(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t unit_id,
               std::optional<bool> can_deploy=true);
std::vector<std::pair<std::string,CanBuild>> options(const session::world::Snapshot&,const FactorySet&,
                                                    const Catalog&,std::uint32_t house_id);
Outcome begin_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
                         const std::string& type,std::uint64_t frame);
Outcome hold_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,std::uint64_t frame);
Outcome cancel_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,std::uint64_t frame);
std::optional<bool> passes_proximity(const session::world::Snapshot&,const FactorySet&,const Catalog&,
                                     std::uint32_t house_id,const std::string& type,CellXY);
// DisplayClass::Set_Cursor_Shape 00498F10: the cursor list is the type's
// Occupy_List(true). Get_Occupy_Dimensions 00499810 gives its width w and
// height h; the footprint origin is the mouse Cell plus (-(w-1)/2,-(h-1)/2).
// The original uses this origin for the grid, proximity and the PLACE event.
// Nothing when the type is not a building with a known foundation.
std::optional<CellXY> placement_origin(const Catalog&,const std::string& type,CellXY mouse);
// One cursor grid for a mouse Cell. A Cell is drawn legal (frame 0 in 00473F20)
// only when the whole cursor passes proximity and that Cell passes 004718B0.
// legal_cell is unknown when proximity or the Cell's own state is unknown.
// TODO-ORIGINAL: 00473F20 and the PLACE click (0049BB20 via 00499390) also need
// the Cell to be revealed; the shroud input is not part of this helper.
struct PlacementGridCell { CellXY cell; std::optional<bool> legal_cell; };
struct PlacementCursor {
    CellXY origin;
    std::optional<bool> proximity;          // 004991D0, display +117C.
    std::optional<bool> legal;              // 006DE0E0: every Cell clear.
    std::vector<PlacementGridCell> cells;   // Occupy_List(true) order.
};
std::optional<PlacementCursor> placement_cursor(const session::world::Snapshot&,const FactorySet&,const Catalog&,
                                                std::uint32_t house_id,const std::string& type,CellXY mouse);
Outcome place(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
              const std::string& type,CellXY,std::uint64_t frame);
// Also places finished Units and Infantry in a free Cell near their factory.
Outcome tick(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint64_t frame);
}
