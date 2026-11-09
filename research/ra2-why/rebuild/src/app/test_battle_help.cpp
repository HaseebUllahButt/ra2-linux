#include "battle_help.h"
#include <algorithm>
#include <cassert>
int main() {
    ra2::app::BattleHelp help;
    ra2::render::Bitmap frame{1024,768,ra2::render::Bytes(1024*768*4,0)};
    help.draw(frame);
    assert(frame.rgba[0] == 0);
    assert(frame.rgba[(200*1024+212)*4] != 0);
    assert(std::count(frame.rgba.begin(),frame.rgba.end(),255) > 1000);
    ra2::render::Bitmap small{17,19,ra2::render::Bytes(17*19*4,0)};
    help.draw(small);
    assert(std::any_of(small.rgba.begin(),small.rgba.end(),[](auto c){return c != 0;}));
    ra2::render::Bitmap invalid{100,100,ra2::render::Bytes(3,7)};
    help.draw(invalid);
    assert(invalid.rgba == ra2::render::Bytes(3,7));
}
