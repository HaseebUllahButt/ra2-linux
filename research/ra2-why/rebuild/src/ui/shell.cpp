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

namespace ra2::ui {
Menu Menu::load(const fs::MixFileSystem& files, const std::vector<std::uint8_t>& exe,
                const ra2text::CsfFile& strings) {
    Menu result;
    result.background_ = draw_shell_background(files, 800, 600);
    result.main_ = load_dialog(exe, Page::Main, strings);
    result.single_ = load_dialog(exe, Page::SinglePlayer, strings);
    result.skirmish_ = load_dialog(exe, Page::Skirmish, strings);
    result.font_ = GameFont::parse(required(files, "GAME.FNT"));
    const auto colors = gfx::Palette::from_bytes(required(files, "SDBTNANM.PAL"));
    const auto button = shape(files, "SDBTNANM.SHP");
    result.normal_ = button.decode_frame(2, colors);
    result.hover_ = button.decode_frame(3, colors);
    result.pressed_ = button.decode_frame(4, colors);
    if (result.normal_.width != 156 || result.normal_.height != 42)
        throw std::runtime_error("Unsupported shell button dimensions");
    return result;
}

std::vector<Menu::Button> Menu::buttons() const {
    const Dialog* dialog = &main_;
    if (page() == Page::SinglePlayer) dialog = &single_;
    if (page() == Page::Skirmish) dialog = &skirmish_;
    std::vector<Button> result;
    for (const auto& control : dialog->controls) {
        const auto id = control.id;
        const bool main_button = page() == Page::Main &&
            (id == 1667 || id == 1668 || id == 1400 || id == 1670 || id == 1372 || id == 1006);
        const bool single_button = page() == Page::SinglePlayer &&
            (id == 1672 || id == 1673 || id == 1401 || id == 1670);
        const bool skirmish_button = page() == Page::Skirmish && (id == 1559 || id == 1472);
        if (!main_button && !single_button && !skirmish_button) continue;
        const bool back = (page() == Page::Main && id == 1006) ||
                          (page() == Page::SinglePlayer && id == 1670) ||
                          (page() == Page::Skirmish && id == 1472);
        // 0x00775BC0 normalizes a 200x100 DLU reference to 300x163 pixels.
        // For these button Y values, normalization followed by cell snapping
        // gives the same rows for reference font heights 8 through 24 pixels.
        // 0x0060B000 snaps to 42-pixel cells; 0x0060B350 places Back last.
        const int pixels_y = control.bounds.y * 163 / 100;
        int row = (pixels_y - 199 + 21) / 42;
        row = std::max(0, std::min(8, row));
        const int y = back ? 535 : 199 + row * 42;
        const bool enabled = (page() == Page::Main && id == 1667) ||
                             (page() == Page::SinglePlayer && (id == 1401 || id == 1670)) ||
                             (page() == Page::Skirmish && id == 1472);
        result.push_back({id, {644, y, 156, 42}, control.label, enabled});
    }
    return result;
}

void Menu::pointer_move(int x, int y) {
    hovered_.reset();
    for (const auto& b : buttons()) {
        if (b.enabled && x >= b.bounds.x && y >= b.bounds.y &&
            x < b.bounds.x + b.bounds.width && y < b.bounds.y + b.bounds.height) {
            hovered_ = b.id;
            break;
        }
    }
}
void Menu::pointer_down(int x, int y) {
    pointer_move(x, y);
    held_ = hovered_;
}
Action Menu::pointer_up(int x, int y) {
    pointer_move(x, y);
    const auto activated = held_ && hovered_ && *held_ == *hovered_ ? held_ : std::nullopt;
    held_.reset();
    if (!activated) return Action::None;
    auto result = navigation_.activate(*activated);
    if (result == Action::PageChanged) hovered_.reset();
    return result;
}

gfx::RgbaImage Menu::render() const {
    auto image = background_;
    for (const auto& b : buttons()) {
        const bool hot = b.enabled && hovered_ == b.id;
        const bool down = hot && held_ == b.id;
        const auto& frame = down ? pressed_ : (hot ? hover_ : normal_);
        blit(image, frame, b.bounds.x, b.bounds.y);
        // 0x0060F9A0 sets the normal text color to COLORREF 0x00ffff.
        // 0x0072A8C0 sets the disabled shell color to RGB (72,0,0).
        // 0x006135D1 centers the caption; pressed text moves by (2,4).
        const auto color = b.enabled ? gfx::Rgba{255,255,0,255} : gfx::Rgba{72,0,0,255};
        const int width = static_cast<int>(font_.text_width(b.label));
        const int x = b.bounds.x + (b.bounds.width - width) / 2 + (down ? 2 : 0);
        const int y = b.bounds.y + 1 + (b.bounds.height - 1 - static_cast<int>(font_.line_height())) / 2 + (down ? 4 : 0);
        font_.draw_text(image, b.label, x, y, color);
    }
    return image;
}
} // namespace ra2::ui
