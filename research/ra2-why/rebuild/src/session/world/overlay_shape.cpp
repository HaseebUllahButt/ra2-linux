#include "overlay_shape.h"
namespace ra2::session::world {
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
