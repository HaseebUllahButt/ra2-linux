#include "actors.h"
#include <filesystem>
#include <array>
#include <memory>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace ra2;
static std::string map_override(const rules::IniFile& map, const std::string& type) {
    const auto* section = map.find_section(type);
    std::string result;
    if (section) for (const auto& entry : section->entries)
        result += entry.key + "=" + entry.value + "\n";
    return result;
}
int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) return 2;
    const bool isolated = argc == 4 && std::string(argv[3]) == "--isolated";
    if (argc == 4 && !isolated) return 2;
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
                types.emplace(type,actors::Kind::Terrain,code->second,map_override(map,type));
            }
        for (const auto& pair : {std::pair{"Infantry",actors::Kind::Infantry},std::pair{"Structures",actors::Kind::Building}}) {
            auto section=map.find_section(pair.first); if (!section) continue;
            for(const auto& entry:section->entries) {
                const auto a=entry.value.find(','); if(a==std::string::npos) continue;
                const auto b=entry.value.find(',',a+1); if(b==std::string::npos) continue;
                auto type=std::string(rules::IniFile::trim(std::string_view(entry.value).substr(a+1,b-a-1)));
                types.emplace(type,pair.second,code->second,map_override(map,type));
            }
        }
    }
    std::map<char,std::unique_ptr<fs::MixFileSystem>> theater_files;
    if (isolated) {
        // 0x005349C0 mounts base, extension, ISO MD, and ISO archives.
        const std::map<char,std::array<const char*,4>> names={
            {'T',{"TEMPERAT.MIX","TEM.MIX","ISOTEMMD.MIX","ISOTEMP.MIX"}},
            {'A',{"SNOW.MIX","SNO.MIX","ISOSNOMD.MIX","ISOSNOW.MIX"}},
            {'U',{"URBAN.MIX","URB.MIX","ISOURBMD.MIX","ISOURB.MIX"}},
            {'D',{"DESERT.MIX","DES.MIX","ISODESMD.MIX","ISODES.MIX"}},
            {'N',{"URBANN.MIX","UBN.MIX","ISOUBNMD.MIX","ISOUBN.MIX"}},
            {'L',{"LUNAR.MIX","LUN.MIX","ISOLUNMD.MIX","ISOLUN.MIX"}}};
        for (const auto& [code, archives] : names) {
            auto current=std::make_unique<fs::MixFileSystem>(argv[1]);
            current->bootstrap(); current->mount_overlays();
            for (auto name : {"CONQMD.MIX","GENERMD.MIX","GENERIC.MIX","ISOGENMD.MIX","ISOGEN.MIX","CONQUER.MIX"}) current->mount(name,false);
            if (code=='A') current->mount("SNOWMD.MIX",false);
            for (auto name : archives) current->mount(name,false);
            theater_files.emplace(code,std::move(current));
        }
    }
    unsigned ok=0, failed=0;
    for(const auto& [type,kind,theater,override_text]:types) {
        rules::IniFile overlay;
        const auto* selected=&rules;
        if (!override_text.empty()) {
            std::string merged = "[" + type + "]\n" + override_text;
            if (const auto* base = rules.find_section(type))
                for (const auto& entry : base->entries) merged += entry.key + "=" + entry.value + "\n";
            merged += std::string(r->begin(),r->end());
            overlay.parse(merged);
            selected=&overlay;
        }
        try { auto& selected_files=isolated ? *theater_files.at(theater) : files; auto actor=actors::ActorArt::load(selected_files,art,*selected,type,kind,theater); actor.draw_frame(0,palette); if (type == "CALA02" || type == "CALOND02") std::cout<<type<<" resolved "<<actor.filename()<<" map-overrides="<<(!override_text.empty())<<"\n"; ++ok; }
        catch(const std::exception& e) { ++failed; std::cout<<type<<' '<<theater<<' '<<e.what()<<'\n'; }
    }
    std::cout<<(isolated ? "isolated " : "all-theaters ")<<"maps="<<maps<<" combinations="<<types.size()<<" loaded="<<ok<<" failed="<<failed<<'\n';
}
