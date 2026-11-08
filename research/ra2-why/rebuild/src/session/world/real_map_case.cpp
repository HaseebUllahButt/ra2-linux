#include "sim/test_inputs.h"
#include "map/map_file.h"
#include "rules/ini_file.h"
#include <algorithm>
#include <cassert>
#include <iostream>

// Check data used by the adapted x86 fixture. Runtime cell state is supplied
// separately by the fixture; this does not reconstruct the full Battle world.
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    ra2::rules::IniFile map, rules, art;
    map.parse(original_test_ini(argv[1],"MP02T2.MAP",true));
    rules.parse(original_test_ini(argv[1],"RULES.INI",true));
    art.parse(original_test_art(argv[1],true));
    assert(map.read_string("Map","Size")=="0,0,100,100");
    assert(map.read_string("Map","LocalSize")=="5,5,90,89");
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
    // 006E4D60/006E5630 use a one-cell footprint for these original types.
    // This proves only the Terrain part of the cell history.
    for (const auto& item:parsed.terrain()) {
        assert(map.key_count(item.type)==0 && rules.read_string(item.type,"Image").empty());
        assert(art.read_string(item.type,"Foundation")=="1x1");
        assert(item.cell.x!=135 || item.cell.y!=80);
    }
    std::cout<<"MP02T2 waypoint, terrain, rules, and Unit registry match the placement fixture\n";
    std::cout<<"No authored Terrain footprint covers MP02T2 waypoint 0\n";
}
