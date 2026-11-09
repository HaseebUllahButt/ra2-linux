#include "overlay_shape.h"
namespace ra2::session::world {
namespace {
std::optional<bool> authored_bool(const std::string& text) {
    std::string lower=text;
    for (auto& ch:lower) if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
    if (lower=="yes" || lower=="true" || lower=="1") return true;
    if (lower=="no" || lower=="false" || lower=="0") return false;
    return {};
}
}
std::optional<OverlayArtFields> base_overlay_art_constructor(const std::string& type) {
    if (type.empty() || type.size()>24) return {};
    return OverlayArtFields{type,false,false,false};
}
std::optional<OverlayArtFields> read_base_overlay_art_slice(const rules::IniFile& rules_layer,
    const rules::IniFile& art,const std::string& type,OverlayArtFields before) {
    if (type.empty() || before.image.empty() || before.image.size()>24) return {};
    if (!rules_layer.find_section(type)) return before;
    if (const auto* image=rules_layer.find(type,"Image")) {
        if (image->value.empty() || image->value.size()>24) return {};
        before.image=image->value;
    }
    for (const auto& item:{std::pair<const char*,bool*>{"Theater",&before.theater},
                           {"NewTheater",&before.new_theater},{"DemandLoad",&before.demand_load}}) {
        if (const auto* value=art.find(before.image,item.first)) {
            const auto parsed=authored_bool(value->value);
            if (!parsed) return {};
            *item.second=*parsed;
        }
    }
    return before;
}
std::optional<OverlayTheaterShape> preload_base_overlay_theater_shape(
    const BaseBattleAssets& assets,const std::string& image,std::optional<bool> theater,
    std::optional<bool> demand,const std::string& suffix) {
    if (!theater || !*theater || !demand || *demand || image.empty() || image.size()>24 || suffix.empty() ||
        suffix.size()>8 || image.find_first_of("./\\\\")!=std::string::npos ||
        suffix.find_first_of("./\\\\")!=std::string::npos) return {};
    const auto filename=image+"."+suffix;
    auto bytes=assets.read_asset(filename);
    if (!bytes || bytes->empty()) return {};
    return OverlayTheaterShape{filename,std::move(*bytes)};
}
}
