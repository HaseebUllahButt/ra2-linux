#include "battle_saves.h"
#include <cassert>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2) { std::cerr<<"usage: test_battle_saves empty_folder\n"; return 2; }
    const std::filesystem::path dir=std::filesystem::path(argv[1])/"saves";
    ra2::app::BattleSaves saves(dir);
    assert(saves.empty());
    const std::string payload("binary\0data\xff",12);
    const auto first=saves.save("First base",payload);
    assert(first && *first=="SAVE0000.SAV");
    const auto second=saves.save("Second",std::string());
    assert(second && *second=="SAVE0001.SAV");
    auto list=saves.list();
    assert(list.size()==2 && list[0].description=="First base" && list[1].file=="SAVE0001.SAV");
    assert(saves.load("SAVE0000.SAV")==payload);
    assert(saves.load("SAVE0001.SAV")==std::string());
    // Overwrite keeps the name.
    assert(saves.save_as("SAVE0000.SAV","Renamed","new"));
    assert(saves.list()[0].description=="Renamed" && saves.load("SAVE0000.SAV")=="new");
    // Names outside the original pattern are refused, so no other file is touched.
    for(const char* bad:{"../SAVE0000.SAV","SAVE000G.SAV","save0000.sav","SAVE0000.SAVX","RA2.INI"}) {
        assert(!ra2::app::BattleSaves::valid_name(bad));
        assert(!saves.save_as(bad,"x","y") && !saves.load(bad) && !saves.remove(bad));
    }
    assert(!saves.save_as("SAVE0002.SAV",std::string(256,'x'),"y"));
    // Damaged and foreign files are not listed and do not load.
    { std::ofstream(dir/"SAVE0003.SAV",std::ios::binary)<<"RA2NSLOT"; }
    { std::ofstream(dir/"notes.txt")<<"hello"; }
    assert(saves.list().size()==2 && !saves.load("SAVE0003.SAV"));
    // A new slot skips used numbers, including damaged files.
    assert(saves.save("Third","p")==std::optional<std::string>("SAVE0002.SAV"));
    assert(saves.save("Fourth","p")==std::optional<std::string>("SAVE0004.SAV"));
    assert(saves.remove("SAVE0001.SAV") && !saves.remove("SAVE0001.SAV"));
    list=saves.list();
    assert(list.size()==3 && list[0].file=="SAVE0000.SAV" && list[1].file=="SAVE0002.SAV");
    assert(!std::filesystem::exists(dir/"SAVE0000.SAV.tmp"));
    std::cout<<"battle saves ok\n";
}
