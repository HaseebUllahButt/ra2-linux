#include "overlay_shape.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    auto assets=BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    const auto* registry=assets.rules().find_section("OverlayTypes"); assert(registry);
    std::array<bool,256> used{};
    for (const auto& entry:assets.overlays_in_scan_order()) used[entry.type_index]=true;
    unsigned count=0;
    for (std::size_t i=0;i<registry->entries.size();++i) if (used[i]) {
        const auto& type=registry->entries[i].value;
        const auto image=assets.rules().read_string(type,"Image",type);
        // The original readers are not yet connected. These flags and the
        // chosen image are supplied here from installed authored records.
        assert(assets.art_ini().read_bool(image,"Theater",false));
        assert(!assets.art_ini().find(image,"DemandLoad"));
        const auto shape=preload_base_overlay_theater_shape(assets,image,true,false,"TEM");
        if (!shape) { std::cerr<<"Unresolved installed shape "<<image<<".TEM\n"; return 1; }
        assert(shape->filename==image+".TEM" && !shape->bytes.empty());
        std::cout<<i<<' '<<image<<' '<<shape->filename<<' '<<shape->bytes.size()<<'\n';
        assert(!preload_base_overlay_theater_shape(assets,image,{},false,"TEM"));
        assert(!preload_base_overlay_theater_shape(assets,image,true,true,"TEM"));
        ++count;
    }
    assert(count==17);
}
