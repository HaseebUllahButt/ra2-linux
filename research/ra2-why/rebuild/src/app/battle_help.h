#pragma once
#include "../render/minimap.h"

namespace ra2::app {
enum class PauseAction { None, Resume, Abort, Back };
// Draw the controls panel over an RGBA frame. The caller owns visibility and input.
class BattleHelp {
public:
    void draw(render::Bitmap& frame) const;
    void draw_pause(render::Bitmap& frame,bool confirm) const;
    static PauseAction pause_action(int x,int y,bool confirm);
};
}
