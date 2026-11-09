#pragma once
#include "../fs/mix_bootstrap.h"
#include "../gfx/shp_decode.h"
#include "../render/tmp_decode.h"
#include <cstdint>
#include <vector>

namespace ra2::app {
enum class CursorKind { Normal, Select, Move, Attack, Deploy, NoGo };
// Prototype API; TODO-ORIGINAL: recover original cursor timing and rendering.
class BattleCursor {
public:
    explicit BattleCursor(const fs::MixFileSystem& files);
    // tick is a game tick; the caller chooses the cursor and advances time.
    void draw(render::Bitmap& target, CursorKind kind, int x, int y,
              std::uint64_t tick = 0) const;
private:
    std::vector<gfx::RgbaImage> frames_;
};
}
