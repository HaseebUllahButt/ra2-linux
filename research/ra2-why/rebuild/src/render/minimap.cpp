#include "minimap.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ra2::render {
namespace {
std::string number(int n, int width) {
    std::ostringstream out; out << std::setfill('0') << std::setw(width) << n;
    return out.str();
}
Rectangle rectangle(const rules::IniFile& ini, std::string_view name) {
    const auto values = ini.read_list("Map", name);
    if (values.size() != 4) throw RenderError("Expected [Map] " + std::string(name) + " rectangle");
    int fields[4];
    for (unsigned i = 0; i < 4; ++i) {
        auto n = rules::IniFile::integer_value(values[i]);
        if (!n || *n < 0 || *n > 512) throw RenderError("Map rectangle exceeds supported grid");
        fields[i] = *n;
    }
    if (!fields[2] || !fields[3]) throw RenderError("Empty map rectangle");
    return {fields[0], fields[1], fields[2], fields[3]};
}
}

MapGeometry MapGeometry::from_ini(const rules::IniFile& ini) {
    MapGeometry g{rectangle(ini, "Size"), rectangle(ini, "LocalSize")};
    // 0x00565C10 normalizes Size's first two fields to zero.
    if (g.size.x || g.size.y) throw RenderError("Expected zero map Size origin");
    if (g.local.x+g.local.width > g.size.width || g.local.y+g.local.height > g.size.height)
        throw RenderError("LocalSize is outside Size");
    return g;
}

TheaterArt TheaterArt::load(fs::MixFileSystem& files, std::string_view theater) {
    struct Theater { const char *name, *root, *iso, *iso_md, *extension; };
    static constexpr Theater theaters[] = {
        {"TEMPERATE", "TEMPERAT", "ISOTEMP", "ISOTEMMD", "TEM"},
        {"SNOW", "SNOW", "ISOSNOW", "ISOSNOMD", "SNO"},
        {"NEWURBAN", "URBANN", "ISOUBN", "ISOUBNMD", "UBN"},
    };
    const Theater* selected = nullptr;
    for (const auto& t : theaters) if (theater == t.name) selected = &t;
    if (!selected) throw RenderError("Unsupported theater: " + std::string(theater));
    const auto& t = *selected;
    // The MIX API searches first mount first. Expansion art must precede base.
    files.mount("ISOGENMD.MIX", false);
    files.mount("ISOGEN.MIX", false);
    files.mount(std::string(t.root)+"MD.MIX", false);
    files.mount(std::string(t.root)+".MIX");
    files.mount(std::string(t.iso_md)+".MIX", false);
    files.mount(std::string(t.iso)+".MIX");
    TheaterArt art; art.files_ = &files;
    art.palette_name_ = "ISO"+std::string(t.extension)+".PAL";
    const auto palette = files.read_file(art.palette_name_);
    if (!palette) throw RenderError("Missing theater palette " + art.palette_name_);
    art.palette_ = gfx::Palette::from_bytes(*palette);
    if (const auto loc = files.locate(art.palette_name_)) art.palette_archive_ = loc->archive->name();
    auto data = files.read_file(std::string(t.root)+"MD.INI");
    if (!data) data = files.read_file(std::string(t.root)+".INI");
    if (!data) throw RenderError("Missing theater tileset INI");
    rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
    const int clear_set = ini.read_integer("General", "ClearTile", 0);
    std::uint32_t wire_count = 0;
    for (int set = 0; set < 4096; ++set) {
        const auto section = "TileSet"+number(set, 4);
        const int count = ini.read_integer(section, "TilesInSet", -1);
        if (count == -1) break; // Same stopping rule as 0x00545150.
        if (count < 0 || count > 4096 || art.filenames_.size()+count > 65535)
            throw RenderError("Invalid TilesInSet");
        if (set == clear_set) art.clear_ = art.filenames_.size();
        const auto stem = ini.read_string(section, "FileName");
        if (count && stem.empty()) throw RenderError("Missing tileset FileName");
        const auto old_count = ini.read_integer(section, "LastTilesInSet", count);
        if (old_count < 0 || old_count > count) throw RenderError("Invalid LastTilesInSet");
        wire_count += old_count;
        if (old_count != count) art.shifts_.emplace_back(wire_count, count-old_count);
        for (int n = 1; n <= count; ++n) {
            art.filenames_.push_back(stem+number(n, 2)+"."+t.extension);
            art.urban_fallback_.push_back(ini.read_integer(section, "NonMarbleMadness", 0xffff) != 0);
        }
    }
    if (art.filenames_.empty() || art.clear_ >= art.filenames_.size())
        throw RenderError("Theater has no valid tileset catalog");
    return art;
}

const DecodedTile& TheaterArt::tile(std::uint32_t index, std::uint8_t subtile) {
    if (index == 0xffff || index == 0xffffffff) { index = clear_; subtile = 0; }
    else {
        const auto original = index;
        for (auto [threshold, shift] : shifts_) {
            if (original < threshold) break;
            if (index > std::numeric_limits<std::uint32_t>::max()-shift)
                throw RenderError("Tile index translation overflow");
            index += shift;
        }
    }
    if (index >= filenames_.size()) throw RenderError("Map tile index outside theater catalog: "+std::to_string(index));
    const auto key = std::make_pair(index, subtile);
    if (const auto found = decoded_.find(key); found != decoded_.end()) return found->second;
    auto container = containers_.find(index);
    if (container == containers_.end()) {
        auto name = filenames_[index];
        auto bytes = files_->read_file(name);
        // 0x00545150, LAB_0054673A: after editor-only marble art attempts,
        // the runtime fallback extension at 0x00829138 is ".URB".
        if (!bytes && urban_fallback_[index]) {
            name.replace(name.find_last_of('.')+1, std::string::npos, "URB");
            bytes = files_->read_file(name);
        }
        if (!bytes) throw RenderError("Missing terrain artwork: "+name);
        try { container = containers_.emplace(index, TmpImage::parse(std::move(*bytes))).first; }
        catch (const RenderError& e) { throw RenderError(name+": "+e.what()); }
    }
    try { return decoded_.emplace(key, container->second.decode(subtile, palette_)).first->second; }
    catch (const RenderError& e) { throw RenderError(filenames_[index]+" subtile "+std::to_string(subtile)+": "+e.what()); }
}

Bitmap render_minimap(const pack::Terrain& terrain, const MapGeometry& geometry, TheaterArt& art) {
    const auto& local = geometry.local;
    if (local.width <= 0 || local.height <= 0 || local.width > 512 || local.height > 512 ||
        local.x < 0 || local.y < 0 || local.x > 512 || local.y > 512 ||
        geometry.size.width <= 0 || geometry.size.width > 512 ||
        geometry.size.height <= 0 || geometry.size.height > 512 ||
        local.x+local.width > geometry.size.width || local.y+local.height > geometry.size.height)
        throw RenderError("Invalid minimap geometry");
    const int tw = art.tile_width(), th = art.tile_height();
    Bitmap image{local.width*tw, local.height*th, {}};
    const auto pixels = std::size_t(image.width)*image.height;
    if (pixels > 64*1024*1024) throw RenderError("Minimap exceeds pixel limit");
    image.rgba.resize(pixels*4);
    std::vector<int> depth(pixels, std::numeric_limits<int>::min());
    std::vector<const pack::Tile*> order;
    for (const auto& tile : terrain.tiles) order.push_back(&tile);
    std::stable_sort(order.begin(), order.end(), [](auto a, auto b) {
        if (a->x+a->y != b->x+b->y) return a->x+a->y < b->x+b->y;
        return int(a->x)-a->y < int(b->x)-b->y;
    });
    for (const auto* tile : order) {
        // 0x006D1F10 projection, normalized to the rectangular map diamond.
        const int x = (int(tile->x)-tile->y+geometry.size.width-1)*(tw/2)-local.x*tw;
        const int ground_y = (int(tile->x)+tile->y-geometry.size.width-1)*(th/2);
        const int y = ground_y-int(tile->height)*(th/2)-local.y*th;
        const auto& decoded = art.tile(tile->index, tile->subtile);
        auto draw = [&](const TileLayer& layer) {
            const auto& b = layer.bitmap;
            const int left = x+layer.x, top = y+layer.y;
            const int x0 = std::max(0, -left), y0 = std::max(0, -top);
            const int x1 = std::min(b.width, image.width-left), y1 = std::min(b.height, image.height-top);
            for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx) {
                const auto source = std::size_t(sy)*b.width+sx;
                if (!b.rgba[source*4+3]) continue;
                const auto target = std::size_t(top+sy)*image.width+left+sx;
                // 0x00547CF0 uses smaller Z as nearer. Reverse its sign here:
                // global - screenY - th - height*th/2 + z -> groundY+th-z.
                const int z = ground_y+th-(layer.z.empty() ? 0 : layer.z[source]);
                if (z < depth[target]) continue;
                depth[target] = z;
                std::copy_n(b.rgba.begin()+source*4, 4, image.rgba.begin()+target*4);
            }
        };
        draw(decoded.diamond);
        if (decoded.extra) draw(*decoded.extra);
    }
    return image;
}

void write_ppm(const Bitmap& image, const std::filesystem::path& path) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() != std::size_t(image.width)*image.height*4)
        throw RenderError("Invalid RGBA image");
    std::ofstream out(path, std::ios::binary);
    if (!out) throw RenderError("Cannot create PPM: "+path.string());
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (std::size_t p = 0; p < image.rgba.size(); p += 4)
        out.write(reinterpret_cast<const char*>(image.rgba.data()+p), 3);
    out.close();
    if (!out) throw RenderError("Failed to write PPM: "+path.string());
}
} // namespace ra2::render
