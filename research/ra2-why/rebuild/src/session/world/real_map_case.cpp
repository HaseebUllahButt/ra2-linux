#include "sim/test_inputs.h"
#include "map/map_file.h"
#include "rules/ini_file.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>

// Check data used by the adapted x86 fixture. Runtime cell state is supplied
// separately by the fixture; this does not reconstruct the full Battle world.
int main(int argc, char** argv) {
    if (argc!=2 && argc!=3) return 2;
    ra2::rules::IniFile map, rules, art;
    map.parse(original_test_ini(argv[1],"MP02T2.MAP",true));
    if (argc==3) {
        const std::string request=argv[2];
        if (request!="--tile-bytes" && request!="--overlay-bytes") return 2;
        const auto bytes=request=="--tile-bytes"?
            ra2::pack::read_section(map,"IsoMapPack5",ra2::pack::Codec::Lzo):
            ra2::pack::read_section(map,"OverlayPack",ra2::pack::Codec::Lcw);
        std::cout.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        return std::cout?0:1;
    }
    rules.parse(original_test_ini(argv[1],"RULES.INI",true));
    art.parse(original_test_art(argv[1],true));
    assert(map.read_string("Map","Size")=="0,0,100,100");
    assert(map.read_string("Map","LocalSize")=="5,5,90,89");
    for (const char* section:{"IsoMapPack","IsoMapPack2","IsoMapPack3","IsoMapPack4"})
        assert(map.key_count(section)==0);
    assert(map.key_count("CellTags")==0);
    assert(map.key_count("AMCV")==0 && map.key_count("Clear")==0 && map.key_count("Water")==0);
    assert(rules.read_string("AMCV","Strength")=="1000");
    assert(rules.read_string("AMCV","Crusher")=="yes");
    assert(rules.read_string("AMCV","SpeedType").empty());
    assert(rules.read_string("Clear","Track")=="100%");
    assert(rules.read_string("Water","Track")=="0%");
    assert(rules.key_count("VehicleTypes")==55 && map.key_count("VehicleTypes")==0);
    const auto* vehicle_types=rules.find_section("VehicleTypes");
    assert(vehicle_types);
    const auto vehicle=std::find_if(vehicle_types->entries.begin(),vehicle_types->entries.end(),
                                    [](const auto& item){return item.value=="AMCV";});
    assert(vehicle!=vehicle_types->entries.end());
    std::cout<<"AMCV VehicleTypes entry: "<<vehicle->key<<"\n";
    ra2map::MapFile parsed; parsed.parse(map);
    const auto waypoint=std::find_if(parsed.waypoints().begin(),parsed.waypoints().end(),
                                     [](const auto& item){return item.index==0;});
    assert(waypoint!=parsed.waypoints().end() && waypoint->cell.x==135 && waypoint->cell.y==80);
    const auto cells=original_test_cells(argv[1],"MP02T2.MAP",true);
    const auto cell=std::find_if(cells.begin(),cells.end(),[](const auto& item){return item.x==135 && item.y==80;});
    assert(cell!=cells.end() && cell->raw==13 && cell->ramp==0 && cell->height==4 && cell->overlay==-1);
    assert(parsed.terrain().size()==206 && parsed.structures().size()==25 && parsed.units().size()==5);
    const auto& overlay=parsed.packed_terrain().overlay;
    if (!overlay.empty()) {
        assert(overlay.size()==512u*512u);
        for (int y=72;y<=88;++y) for (int x=127;x<=143;++x)
            assert(overlay[std::size_t(y)*512+std::size_t(x)]==255);
    }
    // 006E4D60/006E5630 use a one-cell footprint for these original types.
    // This proves only the Terrain part of the cell history.
    for (const auto& item:parsed.terrain()) {
        assert(map.key_count(item.type)==0 && rules.read_string(item.type,"Image").empty());
        assert(art.read_string(item.type,"Foundation")=="1x1");
        assert(std::abs(int(item.cell.x)-135)>17 || std::abs(int(item.cell.y)-80)>17);
    }
    // Base 00458940 supplies the original maximum bounds for these foundations.
    const std::map<std::string,std::pair<int,int>> foundations={
        {"1x1",{1,1}},{"2x2",{2,2}},{"2x3",{2,3}},{"3x2",{3,2}},
        {"3x3",{3,3}},{"3x4",{3,4}},{"4x2",{4,2}},{"4x4",{4,4}},{"5x3",{5,3}}};
    for (const auto& item:parsed.structures()) {
        assert(map.key_count(item.type)==0);
        const auto image=rules.read_string(item.type,"Image",item.type);
        const auto foundation=foundations.find(art.read_string(image,"Foundation","1x1"));
        assert(foundation!=foundations.end());
        const int dx=135-int(item.cell.x), dy=80-int(item.cell.y);
        assert(std::abs(dx)>19 || std::abs(dy)>19);
        // One extra row covers Bib; two cells cover nearby Cell counters.
        assert(dx < -2 || dy < -2 || dx > foundation->second.first+1 || dy > foundation->second.second+2);
    }
    assert(parsed.infantry().empty() && parsed.aircraft().empty());
    for (const auto& item:parsed.units())
        assert(std::abs(int(item.cell.x)-135)>41 || std::abs(int(item.cell.y)-80)>41);
    std::cout<<"MP02T2 waypoint, terrain, rules, and Unit registry match the placement fixture\n";
    std::cout<<"No authored Terrain footprint covers MP02T2 waypoint 0\n";
    std::cout<<"No authored Unit, Structure, or Terrain footprint reaches waypoint 0 or its neighbors\n";
    std::cout<<"No packed overlay appears within eight cells of waypoint 0\n";
    std::cout<<"MP02T2 CellTags entries: "<<map.key_count("CellTags")<<'\n';
}
