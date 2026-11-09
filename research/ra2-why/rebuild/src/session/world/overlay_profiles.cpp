#include "overlay_profiles.h"
#include <array>
#include <set>
namespace ra2::session::world {
std::optional<std::map<std::uint8_t,OverlayTypeInputs>> prepare_base_overlay_inputs(
    const BaseBattleAssets& assets) {
    const auto* registry=assets.rules().find_section("OverlayTypes");
    if (!registry || registry->entries.size()>255 || assets.battle_rules().key_count("OverlayTypes") ||
        assets.map_ini().key_count("OverlayTypes") ||
        assets.map_ini().read_string("Map","Theater")!="TEMPERATE") return {};
    std::set<std::string> unique;
    for (const auto& item:registry->entries) {
        std::string name=item.value;
        for (auto& ch:name) if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        if (name.empty() || name=="<none>" || !unique.insert(name).second) return {};
    }
    std::array<bool,255> used{};
    for (const auto& item:assets.overlays_in_scan_order()) {
        if (item.type_index>=registry->entries.size()) return {};
        used[item.type_index]=true;
    }
    std::map<std::uint8_t,OverlayTypeInputs> result;
    for (std::size_t i=0;i<registry->entries.size();++i) if (used[i]) {
        const auto& name=registry->entries[i].value;
        // These restrictions avoid choosing an unproved order for a changed
        // field, and avoid inventing animation registry lookup results.
        for (const char* key:{"Land","Wall","Tiberium","Crate","Image","CellAnim","Crushable",
                              "NoUseTileLandType","IsARock","IsRubble"})
            if (assets.battle_rules().find(name,key) || assets.map_ini().find(name,key)) return {};
        if (assets.rules().find(name,"CellAnim") || !assets.rules().find_section(name)) return {};
        auto rule=read_base_overlay_rule_slice(assets.rules(),name,base_overlay_type_constructor());
        auto art=base_overlay_art_constructor(name);
        if (!rule || !art) return {};
        art=read_base_overlay_art_slice(assets.rules(),assets.art_ini(),name,*art);
        if (!art) return {};
        auto shape=preload_base_overlay_theater_shape(assets,art->image,art->theater,art->demand_load,"TEM");
        if (!shape) return {};
        result.emplace(static_cast<std::uint8_t>(i),OverlayTypeInputs{name,*rule,*art,std::move(*shape)});
    }
    return result;
}
}
