#include "actors.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace ra2;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    fs::MixFileSystem files(argv[1]);
    files.bootstrap(); files.mount_overlays();
    for (auto name : {"CONQMD.MIX", "CONQUER.MIX", "GENERMD.MIX", "GENERIC.MIX", "ISOGENMD.MIX", "ISOGEN.MIX", "ISOTEMP.MIX", "ISOSNOW.MIX", "ISOURB.MIX", "ISODES.MIX", "ISOLUN.MIX", "ISOUBN.MIX", "ISOTEMMD.MIX", "ISOSNOMD.MIX", "ISOURBMD.MIX", "ISODESMD.MIX", "ISOLUNMD.MIX", "ISOUBNMD.MIX", "TEMPERAT.MIX", "TEM.MIX", "SNOW.MIX", "SNO.MIX", "URBAN.MIX", "URB.MIX", "LUNAR.MIX", "LUN.MIX", "DESERT.MIX", "DES.MIX", "NEWURBAN.MIX", "URBANN.MIX", "URBANNMD.MIX", "UBN.MIX", "TEMPERATMD.MIX", "SNOWMD.MIX", "URBANMD.MIX", "LUNARMD.MIX", "DESERTMD.MIX", "NEWURBANMD.MIX"}) files.mount(name, false);
    auto a=files.read_file("artmd.ini"), r=files.read_file("rulesmd.ini");
    if (!a || !r) return 2;
    auto palette_data=files.read_file("UNITTEM.PAL"); if (!palette_data) return 2;
    auto palette=gfx::Palette::from_bytes(*palette_data);
    rules::IniFile art, rules; art.parse(std::string(a->begin(),a->end())); rules.parse(std::string(r->begin(),r->end()));
    using Key=std::tuple<std::string,actors::Kind,char,std::string>;
    std::set<Key> types; std::size_t maps=0;
    const std::map<std::string,char> codes={{"TEMPERATE",'T'},{"SNOW",'A'},{"URBAN",'U'},{"DESERT",'D'},{"NEWURBAN",'N'},{"LUNAR",'L'}};
    for (const auto& file:std::filesystem::directory_iterator(argv[2])) {
        if (!file.is_regular_file() || file.path().extension() != ".MAP") continue;
        std::ifstream stream(file.path()); rules::IniFile map; map.parse(stream); ++maps;
        auto theater=map.read_string("Map","Theater","TEMPERATE");
        auto code=codes.find(theater); if(code==codes.end()) { std::cerr<<"unknown theater "<<theater<<'\n'; return 1; }
        if (auto terrain=map.find_section("Terrain"))
            for (const auto& entry : terrain->entries)
                {
                auto type = std::string(rules::IniFile::trim(entry.value));
                types.emplace(type,actors::Kind::Terrain,code->second,map.read_string(type,"Image"));
            }
        for (const auto& pair : {std::pair{"Infantry",actors::Kind::Infantry},std::pair{"Structures",actors::Kind::Building}}) {
            auto section=map.find_section(pair.first); if (!section) continue;
            for(const auto& entry:section->entries) {
                const auto a=entry.value.find(','); if(a==std::string::npos) continue;
                const auto b=entry.value.find(',',a+1); if(b==std::string::npos) continue;
                auto type=std::string(rules::IniFile::trim(std::string_view(entry.value).substr(a+1,b-a-1)));
                types.emplace(type,pair.second,code->second,map.read_string(type,"Image"));
            }
        }
    }
    unsigned ok=0, failed=0;
    for(const auto& [type,kind,theater,override_image]:types) {
        rules::IniFile overlay;
        const auto* selected=&rules;
        if (!override_image.empty()) {
            overlay.parse("["+type+"]\nImage="+override_image+"\n"+std::string(r->begin(),r->end()));
            selected=&overlay;
        }
        try { auto actor=actors::ActorArt::load(files,art,*selected,type,kind,theater); actor.draw_frame(0,palette); if (type == "CALA02" || type == "CALOND02") std::cout<<type<<" resolved "<<actor.filename()<<" override="<<override_image<<"\n"; ++ok; }
        catch(const std::exception& e) { ++failed; std::cout<<type<<' '<<theater<<' '<<e.what()<<'\n'; }
    }
    std::cout<<"maps="<<maps<<" combinations="<<types.size()<<" loaded="<<ok<<" failed="<<failed<<'\n';
}
