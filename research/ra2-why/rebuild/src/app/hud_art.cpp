// Original sidebar art loader. See hud_art.h for the sources.
#include "hud_art.h"
#include "../fs/mix_bootstrap.h"
#include "../gfx/shp_decode.h"
#include <algorithm>
#include <cctype>

namespace ra2::app {
namespace {
// Every SHP the sidebar draws. All of them exist in SIDEC01 and SIDEC02.
constexpr const char* side_shps[] = {
    "credits.shp", "top.shp", "radar.shp", "side1.shp", "side2.shp", "side3.shp",
    "addon.shp", "diplobtn.shp", "optbtn.shp", "repair.shp", "sell.shp",
    "tab00.shp", "tab01.shp", "tab02.shp", "tab03.shp", "powerp.shp",
    "r-up.shp", "r-dn.shp", "gclock2.shp", "lspacer.shp", "lendcap.shp",
    "rendcap.shp", "bttnbkgd.shp", "button00.shp", "button01.shp", "button03.shp",
    "button04.shp", "button06.shp", "button09.shp"};
std::vector<HudSprite> decode_all(const std::vector<std::uint8_t>& bytes, const gfx::Palette& palette) {
    auto shp = gfx::ShpImage::parse(bytes);
    std::vector<HudSprite> out;
    for (std::size_t i = 0; i < shp.frames().size(); ++i) {
        auto image = shp.decode_frame(i, palette);
        out.push_back({image.width, image.height, std::move(image.pixels)});
    }
    return out;
}
std::uint32_t decode_utf8(std::string_view s, std::size_t& i) {
    const auto c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    std::uint32_t cp = c & (0x3f >> extra);
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3f);
    return cp;
}
} // namespace

HudSideLayout hud_side_layout(bool soviet) {
    // Matched against reference/shots/05 (Allies) and 03b (Soviets).
    if (soviet) return {{870, 21}, {942, 21}, {889, 165}, {941, 165}, 876, 32};
    return {{867, 20}, {939, 20}, {876, 166}, {940, 166}, 882, 29};
}

bool HudArt::load(const fs::MixFileSystem& files, bool soviet) {
    ready_ = false; sprites_.clear(); font_.reset(); soviet_ = soviet;
    const std::string mix = soviet ? "sidec02.mix" : "sidec01.mix";
    try {
        std::shared_ptr<const fs::MixArchive> archive;
        const auto loose = files.directory() / mix;
        std::error_code error;
        if (std::filesystem::is_regular_file(loose, error)) archive = fs::MixArchive::open(loose);
        else if (const auto at = files.locate(mix)) archive = at->archive->open_nested(at->entry, mix);
        if (!archive) return false;
        const auto read = [&](const char* name) -> std::optional<std::vector<std::uint8_t>> {
            if (const auto* entry = archive->find(name)) return archive->read(*entry);
            return files.read_file(name);
        };
        const auto pal = read("sidebar.pal");
        if (!pal) return false;
        const auto palette = gfx::Palette::from_bytes(*pal);
        for (const char* name : side_shps) {
            const auto bytes = read(name);
            if (!bytes) { sprites_.clear(); return false; }
            sprites_[name] = decode_all(*bytes, palette);
        }
        const auto text_pal = files.read_file("grfxtxt.pal");
        const auto text_shp = files.read_file("grfxtxt.shp");
        if (text_pal && text_shp)
            sprites_["grfxtxt.shp"] = decode_all(*text_shp, gfx::Palette::from_bytes(*text_pal));
        if (auto font = files.read_file("game.fnt"))
            font_ = std::make_shared<ui::GameFont>(ui::GameFont::parse(std::move(*font)));
    } catch (const std::exception&) {
        sprites_.clear(); font_.reset(); return false;
    }
    ready_ = true;
    return true;
}
const HudSprite* HudArt::sprite(std::string_view shp, int frame) const {
    const auto found = sprites_.find(shp);
    if (found == sprites_.end() || frame < 0 || frame >= int(found->second.size())) return nullptr;
    const auto& s = found->second[std::size_t(frame)];
    return s.width > 0 && s.height > 0 ? &s : nullptr;
}
int HudArt::frames(std::string_view shp) const {
    const auto found = sprites_.find(shp);
    return found == sprites_.end() ? 0 : int(found->second.size());
}

void hud_blit(render::Bitmap& b, const HudSprite& s, int x, int y, render::Rectangle clip) {
    const int x0 = std::max({0, x, clip.x}), y0 = std::max({0, y, clip.y});
    const int x1 = std::min({b.width, x + s.width, clip.x + clip.width});
    const int y1 = std::min({b.height, y + s.height, clip.y + clip.height});
    if (b.rgba.size() < std::size_t(std::max(0, b.width)) * std::max(0, b.height) * 4) return;
    for (int yy = y0; yy < y1; ++yy) for (int xx = x0; xx < x1; ++xx) {
        const auto* p = &s.rgba[(std::size_t(yy - y) * s.width + (xx - x)) * 4];
        if (!p[3]) continue;
        std::copy_n(p, 4, &b.rgba[(std::size_t(yy) * b.width + xx) * 4]);
    }
}
void hud_blend(render::Bitmap& b, const HudSprite& s, int x, int y) {
    if (b.rgba.size() < std::size_t(std::max(0, b.width)) * std::max(0, b.height) * 4) return;
    for (int yy = std::max(0, y); yy < std::min(b.height, y + s.height); ++yy)
        for (int xx = std::max(0, x); xx < std::min(b.width, x + s.width); ++xx) {
            const auto* p = &s.rgba[(std::size_t(yy - y) * s.width + (xx - x)) * 4];
            if (!p[3]) continue;
            auto* d = &b.rgba[(std::size_t(yy) * b.width + xx) * 4];
            for (int c = 0; c < 3; ++c) d[c] = std::uint8_t((d[c] + p[c]) / 2);
        }
}
int hud_text(render::Bitmap& b, const ui::GameFont& font, std::string_view text, int x, int y, gfx::Rgba color) {
    if (b.rgba.size() < std::size_t(std::max(0, b.width)) * std::max(0, b.height) * 4) return 0;
    int cursor = x;
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = decode_utf8(text, i);
        if (cp > 0xffff) continue;
        const auto glyph = font.glyph(static_cast<char16_t>(cp), color);
        if (!glyph) continue;
        for (int gy = 0; gy < glyph->height; ++gy) for (int gx = 0; gx < glyph->width; ++gx) {
            const int dx = cursor + gx, dy = y + gy;
            if (dx < 0 || dy < 0 || dx >= b.width || dy >= b.height) continue;
            const auto* p = &glyph->pixels[(std::size_t(gy) * glyph->width + gx) * 4];
            if (p[3]) std::copy_n(p, 4, &b.rgba[(std::size_t(dy) * b.width + dx) * 4]);
        }
        cursor += glyph->width + 1; // 0x00433880: one pixel of spacing.
    }
    return cursor - x;
}
int hud_text_width(const ui::GameFont& font, std::string_view text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto cp = decode_utf8(text, i);
        if (cp > 0xffff) continue;
        if (const auto glyph = font.glyph(static_cast<char16_t>(cp), {})) width += glyph->width + 1;
    }
    return width;
}
} // namespace ra2::app
