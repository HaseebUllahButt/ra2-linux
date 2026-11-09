#pragma once
#include "overlay_rules.h"
#include "overlay_shape.h"
#include <map>
namespace ra2::session::world {
struct OverlayTypeInputs {
    std::string name;
    OverlayRuleFields rules;
    OverlayArtFields art;
    OverlayTheaterShape shape;
};
// Prepare only types named by packed Overlay bytes. These are asset-backed
// reader inputs, not complete runtime types or a completed Overlay phase.
// The checked path requires the base registry, unchanged relevant mode/map
// keys, absent CellAnim, temperate theater, and directly available shapes.
std::optional<std::map<std::uint8_t,OverlayTypeInputs>> prepare_base_overlay_inputs(
    const BaseBattleAssets&);
}
