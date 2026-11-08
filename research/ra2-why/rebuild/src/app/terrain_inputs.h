#pragma once
#include "../render/minimap.h"
#include "../sim/world.h"

namespace ra2::app {
// Supply recovered map fields. Final land and bridge state are resolved by
// the simulation after overlays, map rules, and object placement.
sim::TerrainCells terrain_inputs(const pack::Terrain& terrain, render::TheaterArt& art);
sim::TheaterInputs theater_inputs(render::TheaterArt& art);
// Keep the packed source separate from the live Cell indexes after both passes.
pack::Terrain initialized_terrain(const pack::Terrain& source, const sim::TerrainCells& cells);
}
