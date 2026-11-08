#pragma once
#include "replacement.h"
namespace ra2::sim {
// Base 0055A550/0055A490; expansion 00578350/00578290.
std::vector<ra2map::Cell> original_map_cell_order(int width,int height);
// One ordered 0047D2B0 tile/height/land pass. Overlay land and effect objects stay unresolved.
// first_pass resets Cell ramp state to the original constructor value before iteration.
void initialize_original_tile_pass(TerrainCells&,const TheaterInputs&,int width,int height,std::uint8_t cliff,bool first_pass);
}
