#include "prototype_victory.h"
#include <iostream>
#include <stdexcept>
#include <string>
namespace sw=ra2::session::world;
using ra2::sim::Victory;
using ra2::sim::VictoryState;
using ra2::sim::evaluate_prototype_victory;
namespace {
const char* name(Victory victory) {
    switch (victory) {
        case Victory::InProgress: return "InProgress";
        case Victory::PlayerWon: return "PlayerWon";
        case Victory::PlayerLost: return "PlayerLost";
        case Victory::Draw: return "Draw";
    }
    return "?";
}
void expect(Victory actual,Victory expected,const char* what) {
    if (actual!=expected)
        throw std::runtime_error(std::string(what)+": expected "+name(expected)+
                                 " got "+name(actual));
}
sw::House make_house(std::uint32_t id,const char* role_name,sw::HouseRole role) {
    sw::House house;
    house.id=id; house.name=role_name; house.country=role_name; house.role=role;
    return house;
}
sw::Entity make_entity(std::uint32_t id,std::uint32_t house,sw::Kind kind,
    sw::Lifetime lifetime,std::optional<std::int32_t> hp) {
    sw::Entity entity;
    entity.id=id; entity.house_id=house; entity.kind=kind; entity.lifetime=lifetime;
    entity.type="TEST"; entity.resolved_hp=hp;
    return entity;
}
sw::Snapshot world_with(const sw::House& player,const sw::House& enemy) {
    sw::Snapshot world;
    world.houses={player,enemy};
    return world;
}
void expect_throws(const sw::Snapshot& world,std::uint32_t player,
    std::uint32_t enemy,const char* what) {
    bool thrown=false;
    try { (void)evaluate_prototype_victory(world,player,enemy); }
    catch (const std::invalid_argument&) { thrown=true; }
    if (!thrown) throw std::runtime_error(std::string(what)+": expected invalid_argument");
}
}

int main() {
    const auto player=make_house(1,"Player",sw::HouseRole::Player);
    const auto enemy=make_house(2,"Enemy",sw::HouseRole::Player);
    const auto neutral=make_house(3,"Neutral",sw::HouseRole::Neutral);

    // Empty armies: both houses exist and own nothing.
    {
        const auto world=world_with(player,enemy);
        expect(evaluate_prototype_victory(world,1,2),Victory::Draw,"empty armies");
    }
    // One side gone: the enemy owns nothing.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerWon,"enemy gone");
    }
    // One side gone: the player owns nothing.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,2,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerLost,"player gone");
    }
    // Both gone: every object on both sides is Destroyed.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Destroyed,0));
        world.entities.push_back(make_entity(5,2,sw::Kind::Structure,sw::Lifetime::Destroyed,0));
        expect(evaluate_prototype_victory(world,1,2),Victory::Draw,"both gone");
    }
    // Living construction yards keep the match running.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        world.entities.push_back(make_entity(5,2,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        expect(evaluate_prototype_victory(world,1,2),Victory::InProgress,"living construction yards");
    }
    // Known zero and negative health are dead; unknown health stays alive.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,0));
        world.entities.push_back(make_entity(5,2,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerLost,"zero health");
    }
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,-5));
        world.entities.push_back(make_entity(5,2,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerLost,"negative health");
    }
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,{}));
        world.entities.push_back(make_entity(5,2,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        expect(evaluate_prototype_victory(world,1,2),Victory::InProgress,"unknown health");
    }
    // Created objects count before placement.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Created,100));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerWon,"created unit");
    }
    // Terrain and Aircraft never count in this prototype.
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Terrain,sw::Lifetime::Placed,{}));
        expect(evaluate_prototype_victory(world,1,2),Victory::Draw,"terrain only");
    }
    {
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,100));
        world.entities.push_back(make_entity(5,2,sw::Kind::Aircraft,sw::Lifetime::Placed,100));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerWon,"aircraft only");
    }
    // Map-neutral houses never count for either side.
    {
        auto world=world_with(player,enemy);
        world.houses.push_back(neutral);
        world.entities.push_back(make_entity(4,3,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        expect(evaluate_prototype_victory(world,1,2),Victory::Draw,"neutral structure only");
    }
    {
        auto world=world_with(player,enemy);
        world.houses.push_back(neutral);
        world.entities.push_back(make_entity(4,3,sw::Kind::Structure,sw::Lifetime::Placed,1000));
        world.entities.push_back(make_entity(5,2,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(evaluate_prototype_victory(world,1,2),Victory::PlayerLost,"neutral cannot save a side");
    }
    // Argument errors throw and never return a verdict.
    {
        const auto world=world_with(player,enemy);
        expect_throws(world,0,2,"zero player id");
        expect_throws(world,1,0,"zero enemy id");
        expect_throws(world,1,1,"matching ids");
        expect_throws(world,1,9,"unknown enemy id");
    }
    {
        auto world=world_with(player,enemy);
        world.houses.push_back(neutral);
        expect_throws(world,3,2,"neutral player house");
        expect_throws(world,1,3,"neutral enemy house");
    }
    // The verdict is a pure function of one snapshot.
    {
        const auto world=world_with(player,enemy);
        const auto first=evaluate_prototype_victory(world,1,2);
        const auto second=evaluate_prototype_victory(world,1,2);
        expect(first,second,"repeat call");
        expect(first,Victory::Draw,"repeat value");
    }
    // The first settled verdict latches; the world cannot change it.
    {
        VictoryState state;
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(state.update(world,1,2),Victory::PlayerWon,"latch settles");
        world.entities.push_back(make_entity(5,2,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(state.update(world,1,2),Victory::PlayerWon,"latch holds");
        if (!state.settled()) throw std::runtime_error("latch flag: expected settled");
    }
    {
        VictoryState state;
        auto world=world_with(player,enemy);
        expect(state.update(world,1,2),Victory::Draw,"draw settles");
        world.entities.push_back(make_entity(5,2,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(state.update(world,1,2),Victory::Draw,"draw latch holds");
    }
    {
        VictoryState state;
        auto world=world_with(player,enemy);
        world.entities.push_back(make_entity(4,1,sw::Kind::Unit,sw::Lifetime::Placed,100));
        world.entities.push_back(make_entity(5,2,sw::Kind::Unit,sw::Lifetime::Placed,100));
        expect(state.update(world,1,2),Victory::InProgress,"running does not settle");
        if (state.settled()) throw std::runtime_error("running flag: expected unsettled");
        world.entities[0].lifetime=sw::Lifetime::Destroyed;
        expect(state.update(world,1,2),Victory::PlayerLost,"late loss settles");
    }
    {
        // Original rule, base 004E4110.
        using ra2::sim::VictoryRules;
        using ra2::sim::evaluate_original_victory;
        VictoryRules quick; quick.short_game=true; quick.base_units={"AMCV","SMCV","IGNORED"};
        VictoryRules full=quick; full.short_game=false;
        auto world=world_with(player,enemy);
        auto add=[&](std::uint32_t id,std::uint32_t house,sw::Kind kind,const char* type,
                     sw::Lifetime life=sw::Lifetime::Placed,std::optional<std::int32_t> hp=100) {
            auto e=make_entity(id,house,kind,life,hp); e.type=type; world.entities.push_back(e);
        };
        // Match start: each side has only its base unit.
        add(10,1,sw::Kind::Unit,"amcv"); add(11,2,sw::Kind::Unit,"SMCV");
        expect(evaluate_original_victory(world,1,2,quick),Victory::InProgress,"short game: base units keep both alive");
        // The enemy keeps tanks and infantry, but has no building and no base unit.
        world.entities[1].type="HTNK"; add(12,2,sw::Kind::Infantry,"E2");
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerWon,"short game: army alone does not save a house");
        expect(evaluate_original_victory(world,1,2,full),Victory::InProgress,"long game: army keeps a house alive");
        // The third BaseUnit entry is not read, as in the original.
        world.entities[1].type="IGNORED";
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerWon,"short game: only two base unit types count");
        // One building keeps a house alive in both modes.
        add(13,2,sw::Kind::Structure,"NAPOWR");
        expect(evaluate_original_victory(world,1,2,quick),Victory::InProgress,"short game: one building keeps a house alive");
        // A building that is not on the map, or a dead one, does not count.
        world.entities.back().lifetime=sw::Lifetime::Created;
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerWon,"short game: unplaced building does not count");
        world.entities.back().lifetime=sw::Lifetime::Placed; world.entities.back().resolved_hp=0;
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerWon,"short game: dead building does not count");
        // Long game: aircraft alone keep a house alive; nothing left is a loss.
        world.entities.erase(world.entities.begin()+1,world.entities.end());
        add(14,2,sw::Kind::Aircraft,"ORCA");
        expect(evaluate_original_victory(world,1,2,full),Victory::InProgress,"long game: aircraft count");
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerWon,"short game: aircraft do not count");
        world.entities.pop_back();
        expect(evaluate_original_victory(world,1,2,full),Victory::PlayerWon,"long game: empty house falls");
        // The player falls by the same rule; both fallen is a draw.
        world.entities[0].type="MTNK"; add(15,2,sw::Kind::Structure,"NACNST");
        expect(evaluate_original_victory(world,1,2,quick),Victory::PlayerLost,"short game: player without base falls");
        world.entities.pop_back();
        expect(evaluate_original_victory(world,1,2,quick),Victory::Draw,"short game: both fallen");
        VictoryState latch;
        add(16,2,sw::Kind::Structure,"NACNST"); world.entities[0].type="AMCV";
        expect(latch.update(world,1,2,quick),Victory::InProgress,"original latch stays open");
        world.entities.pop_back();
        expect(latch.update(world,1,2,quick),Victory::PlayerWon,"original latch settles");
        add(17,2,sw::Kind::Structure,"NACNST");
        expect(latch.update(world,1,2,quick),Victory::PlayerWon,"original latch holds");
        bool thrown=false;
        try { (void)evaluate_original_victory(world,1,1,quick); } catch (const std::invalid_argument&) { thrown=true; }
        if (!thrown) throw std::runtime_error("original rule accepted equal houses");
    }
    {
        // Several enemies: each house falls alone; the player wins after the last one.
        using ra2::sim::VictoryRules;
        VictoryRules quick; quick.base_units={"AMCV","SMCV"};
        sw::Snapshot world;
        world.houses={player,enemy,make_house(3,"Third",sw::HouseRole::Player)};
        auto add=[&](std::uint32_t id,std::uint32_t house,sw::Kind kind,const char* type) {
            auto e=make_entity(id,house,kind,sw::Lifetime::Placed,100); e.type=type; world.entities.push_back(e);
        };
        add(20,1,sw::Kind::Structure,"GACNST"); add(21,2,sw::Kind::Structure,"NACNST");
        add(22,3,sw::Kind::Unit,"SMCV"); add(23,2,sw::Kind::Unit,"HTNK");
        VictoryState state;
        const std::vector<std::uint32_t> enemies{2,3};
        expect(state.update(world,1,enemies,quick),Victory::InProgress,"three houses: all alive");
        world.entities[1].resolved_hp=0;
        expect(state.update(world,1,enemies,quick),Victory::InProgress,"three houses: one enemy left");
        if (!state.fallen(2) || state.fallen(3) || state.fallen(1)) throw std::runtime_error("three houses: wrong fallen set");
        // A fallen house stays fallen, even with a new building.
        add(24,2,sw::Kind::Structure,"NAPOWR");
        expect(state.update(world,1,enemies,quick),Victory::InProgress,"three houses: latch per house");
        if (!state.fallen(2)) throw std::runtime_error("three houses: fallen house came back");
        world.entities[2].lifetime=sw::Lifetime::Destroyed;
        expect(state.update(world,1,enemies,quick),Victory::PlayerWon,"three houses: last enemy falls");
        VictoryState lost;
        world.entities[2].lifetime=sw::Lifetime::Placed; world.entities[0].resolved_hp=0;
        expect(lost.update(world,1,enemies,quick),Victory::PlayerLost,"three houses: player falls first");
        bool thrown=false;
        try { VictoryState bad; (void)bad.update(world,1,std::vector<std::uint32_t>{},quick); }
        catch (const std::invalid_argument&) { thrown=true; }
        if (!thrown) throw std::runtime_error("empty enemy list was accepted");
    }
    std::cout<<"Prototype victory spot checks passed: empty armies, one side gone, "
               "both gone, living construction yards, filters, errors and the latch.\n";
    return 0;
}
