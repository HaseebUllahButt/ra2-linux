#include "overlay_rules.h"
#include "sim/test_inputs.h"
#include "pack/map_pack.h"
#include <array>
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    rules::IniFile ini; ini.parse(original_test_ini(argv[1],"RULES.INI",true));
    const auto* registry=ini.find_section("OverlayTypes"); assert(registry && registry->entries.size()==243);
    rules::IniFile map,mode;
    map.parse(original_test_ini(argv[1],"MP02T2.MAP",true));
    mode.parse(original_test_ini(argv[1],"MPBattle.ini",true));
    const auto packed=pack::load_terrain(map);
    assert(packed.overlay.size()==512u*512u && map.key_count("OverlayTypes")==0 && mode.key_count("OverlayTypes")==0);
    std::array<bool,256> used{};
    for (const auto value:packed.overlay) if (value!=255) used[value]=true;
    for (std::size_t i=0;i<registry->entries.size();++i) {
        const auto& type=registry->entries[i].value;
        const auto fields=read_base_overlay_rule_slice(ini,type,base_overlay_type_constructor()); assert(fields);
        auto text=[&](const char* key) {
            const auto* item=ini.find(type,key);
            return item?item->value:std::string("-");
        };
        if (used[i]) for (const char* key:{"Land","Wall","Tiberium","Crate","CellAnim"})
            assert(!map.find(type,key) && !mode.find(type,key));
        // CellAnim lookup uses an animation registry and is not covered here.
        assert(!ini.find(type,"CellAnim"));
        std::cout<<i<<' '<<type<<' '<<int(ini.find_section(type)!=nullptr)<<' '
                 <<text("Land")<<' '<<text("Wall")<<' '<<text("Tiberium")<<' '<<text("Crate")<<' '
                 <<fields->land<<' '<<fields->wall<<' '<<fields->tiberium<<' '<<fields->crate<<'\n';
    }
    rules::IniFile overlay; overlay.parse("[X]\nLand=Clear\nTiberium=yes\nWall=no\nCrate=1\n");
    const auto changed=read_base_overlay_rule_slice(overlay,"X",base_overlay_type_constructor());
    assert(changed && changed->land==5 && !changed->wall && changed->tiberium && changed->crate);
    overlay.parse("[X]\nWall=unresolved\n");
    assert(!read_base_overlay_rule_slice(overlay,"X",base_overlay_type_constructor()));
    overlay.parse("[Other]\nLand=Water\n");
    const auto untouched=read_base_overlay_rule_slice(overlay,"X",*changed);
    assert(untouched && untouched->land==5 && untouched->tiberium && untouched->crate);
}
