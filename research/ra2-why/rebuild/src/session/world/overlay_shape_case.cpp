#include "overlay_shape.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=2 && !(argc==3 && std::string(argv[2])=="--reader")) return 2;
    auto assets=BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    const auto* registry=assets.rules().find_section("OverlayTypes"); assert(registry);
    std::array<bool,256> used{};
    for (const auto& entry:assets.overlays_in_scan_order()) used[entry.type_index]=true;
    unsigned count=0;
    for (std::size_t i=0;i<registry->entries.size();++i) if (used[i]) {
        const auto& type=registry->entries[i].value;
        auto fields=base_overlay_art_constructor(type); assert(fields);
        fields=read_base_overlay_art_slice(assets.rules(),assets.art_ini(),type,*fields); assert(fields);
        fields=read_base_overlay_art_slice(assets.battle_rules(),assets.art_ini(),type,*fields); assert(fields);
        fields=read_base_overlay_art_slice(assets.map_ini(),assets.art_ini(),type,*fields); assert(fields);
        const auto& image=fields->image;
        if (argc==3) {
            const auto read=[&](const rules::IniFile& ini,const std::string& section,const char* key) {
                const auto* item=ini.find(section,key); return item?item->value:std::string("-");
            };
            assert(!assets.battle_rules().find(type,"Image") && !assets.map_ini().find(type,"Image"));
            std::cout<<i<<' '<<type<<' '<<read(assets.rules(),type,"Image")<<' '<<image<<' '
                     <<read(assets.art_ini(),image,"Theater")<<' '<<read(assets.art_ini(),image,"NewTheater")<<' '
                     <<read(assets.art_ini(),image,"DemandLoad")<<' '<<fields->theater<<' '
                     <<fields->new_theater<<' '<<fields->demand_load<<'\n';
            ++count; continue;
        }
        assert(fields->theater && !fields->new_theater && !fields->demand_load);
        const auto shape=preload_base_overlay_theater_shape(assets,image,fields->theater,fields->demand_load,"TEM");
        if (!shape) { std::cerr<<"Unresolved installed shape "<<image<<".TEM\n"; return 1; }
        assert(shape->filename==image+".TEM" && !shape->bytes.empty());
        std::cout<<i<<' '<<image<<' '<<shape->filename<<' '<<shape->bytes.size()<<'\n';
        assert(!preload_base_overlay_theater_shape(assets,image,{},false,"TEM"));
        assert(!preload_base_overlay_theater_shape(assets,image,true,true,"TEM"));
        ++count;
    }
    assert(count==17);
}
