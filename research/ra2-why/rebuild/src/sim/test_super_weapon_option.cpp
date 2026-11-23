// The Super Weapons setup option, base House::Can_Build 004E3832, with the
// installed rules: the option removes only the buildings whose super weapon
// has DisableableFromShell=yes.
#include "prototype_production.h"
#include "production_runtime.h"
#include "sim.h"
#include "session/world/setup.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace w=ra2::session::world;
namespace p=ra2::sim::production;
namespace proto=ra2::sim::production::prototype;
namespace {
void require(bool ok,const std::string& what) { if (!ok) throw std::runtime_error(what); }
}

int main(int argc,char** argv) try {
    require(argc==2,"usage: test_super_weapon_option GAME_DIRECTORY");
    auto assets=w::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    w::PrototypeBattleSetup setup;
    setup.players={{"Player","Americans",20000,0,0,2,true},{"Enemy","Russians",20000,1,1,2,false}};
    const auto loaded=w::prototype_battle(assets,setup,ra2::sim::Random(42).state());
    require(loaded.has_value(),"cannot load MP02T2 prototype");
    const auto catalog=p::Catalog::from(assets.rules(),assets.art_ini(),assets.map_ini());
    const auto disableable=[&](const char* building) {
        const auto* type=catalog.find(building);
        require(type && type->can.super_weapon!=-1,std::string(building)+" has no super weapon");
        const auto found=catalog.super_weapon_disableable.find(type->can.super_weapon);
        require(found!=catalog.super_weapon_disableable.end(),std::string(building)+" super weapon is not registered");
        return found->second;
    };
    for (const char* building:{"NAMISL","NAIRON","GAWEAT","GACSPH"})
        require(disableable(building),std::string(building)+" must be disableable from the setup screen");
    for (const char* building:{"AMRADR","CAAIRP"})
        require(!disableable(building),std::string(building)+" must stay when Super Weapons is off");
    require(catalog.find("NAMISL")->can.super_weapon!=catalog.find("NAIRON")->can.super_weapon,
            "super weapon buildings share one index");
    // A computer house skips prerequisites (004E3878), so its list shows the option alone.
    const auto state=[&](bool allowed,std::uint32_t house,const std::string& type) {
        auto world=loaded->snapshot;
        p::FactorySet factories;
        factories.flags.super_weapons=allowed;
        const auto started=proto::initialize(world,factories,catalog,world.houses[0].id);
        require(started.status==p::Status::Done,"initialize failed: "+started.detail);
        require(factories.flags.super_weapons && *factories.flags.super_weapons==allowed,
                "initialize changed the Super Weapons choice");
        for (const auto& [id,result]:proto::options(world,factories,catalog,house)) if (id==type) return result;
        throw std::runtime_error(type+" is missing from the build list");
    };
    const auto enemy=loaded->snapshot.houses[1].id;
    require(state(true,enemy,"NAMISL")==p::CanBuild::Can,"Nuclear Silo must be allowed with Super Weapons on");
    require(state(true,enemy,"NAIRON")==p::CanBuild::Can,"Iron Curtain must be allowed with Super Weapons on");
    require(state(false,enemy,"NAMISL")==p::CanBuild::Cannot,"Nuclear Silo must be refused with Super Weapons off");
    require(state(false,enemy,"NAIRON")==p::CanBuild::Cannot,"Iron Curtain must be refused with Super Weapons off");
    require(state(false,enemy,"NAPOWR")==p::CanBuild::Can,"Tesla Reactor must stay with Super Weapons off");
    std::cout<<"Super Weapons option checks passed: four switchable buildings, two that stay, both settings.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<"test_super_weapon_option: "<<error.what()<<'\n';
    return 1;
}
