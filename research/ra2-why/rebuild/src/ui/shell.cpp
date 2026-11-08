#include "ui.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::ui {
namespace {
std::vector<std::uint8_t> required(const fs::MixFileSystem& files, const char* name) {
    auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing shell asset: ") + name);
    return std::move(*bytes);
}

gfx::ShpImage shape(const fs::MixFileSystem& files, const char* name) {
    return gfx::ShpImage::parse(required(files, name));
}

void blit(gfx::RgbaImage& dst, const gfx::RgbaImage& src, int x, int y) {
    for (int sy = 0; sy < src.height; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dst.height) continue;
        for (int sx = 0; sx < src.width; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dst.width) continue;
            const auto si = (static_cast<std::size_t>(sy) * src.width + sx) * 4;
            if (!src.pixels[si + 3]) continue;
            const auto di = (static_cast<std::size_t>(dy) * dst.width + dx) * 4;
            std::copy_n(src.pixels.begin() + si, 4, dst.pixels.begin() + di);
        }
    }
}
} // namespace

// 0x006040B0 identifies these controls on dialogs 226, 256 and 258.
Action MenuNavigation::activate(std::uint32_t id) noexcept {
    if (page_ == Page::Main && id == 1667) page_ = Page::SinglePlayer;
    else if (page_ == Page::SinglePlayer && id == 1401) page_ = Page::Skirmish;
    else if (page_ == Page::SinglePlayer && id == 1670) page_ = Page::Main;
    else if (page_ == Page::Skirmish && id == 1472) page_ = Page::SinglePlayer;
    else return Action::None;
    return Action::PageChanged;
}

gfx::RgbaImage draw_shell_background(const fs::MixFileSystem& files, unsigned width, unsigned height) {
    if (width != 800 || height != 600)
        throw std::runtime_error("Shell background currently supports only 800x600");
    // 0x0072E5B9 uses SHELL2.PAL for the tiles. 0x0072E62E uses
    // SDBTNANM.PAL for the button cells. The other pieces use SHELL.PAL.
    const auto pal = gfx::Palette::from_bytes(required(files, "SHELL.PAL"));
    const auto tile_pal = gfx::Palette::from_bytes(required(files, "SHELL2.PAL"));
    const auto button_pal = gfx::Palette::from_bytes(required(files, "SDBTNANM.PAL"));
    const auto main = shape(files, "MNSCRNL.SHP").decode_frame(0, pal);
    const auto lower = shape(files, "LWSCRNL.SHP").decode_frame(0, pal);
    const auto top = shape(files, "SDTP.SHP").decode_frame(0, pal);
    const auto tile = shape(files, "SDBTNBKGD.SHP").decode_frame(0, tile_pal);
    const auto button = shape(files, "SDBTNANM.SHP").decode_frame(10, button_pal);
    const auto bottom = shape(files, "SDBTM.SHP").decode_frame(0, pal);
    if (top.width != tile.width || !tile.height || button.width > tile.width ||
        lower.width != main.width || main.width + top.width != width ||
        main.height + lower.height != height || top.height > height)
        throw std::runtime_error("Unsupported shell asset dimensions");
    gfx::RgbaImage out{static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                       std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
    // 0x0072EC70 builds the rectangles. 0x0072E450 draws the panel,
    // frame 10 of each button cell, and the clipped bottom piece.
    // 0x0072E730 draws the large main image. 0x0072E450 draws its lower edge.
    blit(out, main, 0, 0);
    blit(out, lower, 0, height - lower.height);
    const int panel_x = static_cast<int>(width) - top.width;
    blit(out, top, panel_x, 0);
    const int count = (height - top.height) / tile.height;
    for (int i = 0; i < count; ++i) {
        const int y = top.height + i * tile.height;
        blit(out, tile, panel_x, y);
        blit(out, button, width - button.width, y);
    }
    blit(out, bottom, width - bottom.width, top.height + count * tile.height);
    return out;
}
} // namespace ra2::ui
