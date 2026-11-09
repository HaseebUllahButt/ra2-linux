#include "battle_cursor.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::app {
namespace {
struct Spec { unsigned first, count, delay; bool centered; };
// Frame ranges and hot spots: Vinifera mousetype.cpp, MouseControlType table.
// TODO-ORIGINAL: confirm base RA2 timing and palette selection.
Spec spec(CursorKind kind) {
    switch (kind) {
    case CursorKind::Normal: return {0,1,1,false};
    case CursorKind::Select: return {18,13,4,true};
    case CursorKind::Move: return {31,10,4,true};
    case CursorKind::Attack: return {58,5,4,true};
    case CursorKind::Deploy: return {110,9,4,true};
    case CursorKind::NoGo: return {41,1,1,true};
    }
    throw std::invalid_argument("invalid cursor kind");
}
}
BattleCursor::BattleCursor(const fs::MixFileSystem& files) {
    auto shape = files.read_file("mouse.sha");
    // TODO-ORIGINAL: base installation stores the cursor as MOUSE.SHP.
    if (!shape) shape = files.read_file("mouse.shp");
    auto palette = files.read_file("mouse.pal");
    if (!palette) palette = files.read_file("temperat.pal");
    if (!shape || !palette) throw std::runtime_error("missing cursor shape or palette");
    auto shp = gfx::ShpImage::parse(std::move(*shape));
    if (shp.frames().size() < 119) throw std::runtime_error("cursor shape has too few frames");
    auto pal = gfx::Palette::from_bytes(*palette);
    frames_.reserve(119);
    for (std::size_t i = 0; i < 119; ++i) frames_.push_back(shp.decode_frame(i, pal));
}
void BattleCursor::draw(render::Bitmap& target, CursorKind kind, int x, int y, std::uint64_t tick) const {
    if (target.width <= 0 || target.height <= 0 ||
        std::uint64_t(target.width)*target.height*4 > target.rgba.size()) return;
    const auto s = spec(kind);
    const auto& image = frames_.at(s.first + (tick / s.delay) % s.count);
    const auto left = std::int64_t(x) - (s.centered ? image.width / 2 : 0);
    const auto top = std::int64_t(y) - (s.centered ? image.height / 2 : 0);
    for (int iy=0; iy<image.height; ++iy) for (int ix=0; ix<image.width; ++ix) {
        auto dx=left+ix, dy=top+iy;
        if (dx<0 || dy<0 || dx>=target.width || dy>=target.height) continue;
        auto src=(std::size_t(iy)*image.width+ix)*4;
        if (!image.pixels[src+3]) continue;
        auto dst=(std::size_t(dy)*target.width+std::size_t(dx))*4;
        std::copy_n(image.pixels.begin()+src,4,target.rgba.begin()+dst);
    }
}
}
