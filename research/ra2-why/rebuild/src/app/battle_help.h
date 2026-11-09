#pragma once
#include "../render/minimap.h"

namespace ra2::app {
// Draw the controls panel over an RGBA frame. The caller owns visibility and input.
class BattleHelp {
public:
    void draw(render::Bitmap& frame) const;
};
}
