#include "session.h"
#include "placement_terrain.h"
#include "../sim/distance.h"
#include "../sim/sim.h"
#include "../fs/mix_bootstrap.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string_view>

using namespace ra2;
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    const std::string root=argv[1];
    assert(session::multiplayer_house_color(-2)==5);
    assert(session::multiplayer_house_color(-1)==-1);
    assert(session::multiplayer_house_color(0)==3);
    assert(session::multiplayer_house_color(7)==15);
    assert(session::multiplayer_house_color(9)==9);
    for (int money : {0,2500,5000,10000}) for (int color=0;color<8;++color) {
        const auto fields=session::initialize_house_values(money,color);
        assert(fields.initial_credits==money && fields.cash==money &&
               fields.color==color && fields.house_type_color==color);
    }
    const auto base=sim::OriginalDistance::from_executable(root+"/game.exe");
    const auto yuri=sim::OriginalDistance::from_executable(root+"/gamemd.exe");
    const std::vector<session::StartPoint> points{{10,10},{13,14},{20,10},{30,10}};
    sim::Random random(123), expected(123);
    std::vector<bool> used(points.size(),false);
    const auto draw=expected.ranged(0,3);
    auto first=session::choose_start_point(points,used,random,base);
    assert(first && *first==static_cast<std::size_t>(draw));
    assert(random.table()==expected.table() && random.first()==expected.first());
    used={true,false,false,false};
    const auto before=random.table();
    auto farthest_one=session::choose_start_point(points,used,random,base);
    assert(farthest_one && *farthest_one==3 && random.table()==before);
    used={true,true,false,false};
    const auto ai=session::choose_start_point(points,used,random,base);
    assert(ai && *ai==3);
    assert(random.table()==expected.table() && random.first()==expected.first());
    used={true,true,true,false};
    const auto last=session::choose_start_point(points,used,random,base);
    assert(last && *last==3);
    const std::vector<session::StartPoint> more{{0,0},{4,0},{8,0},{16,0},{30,0}};
    std::vector<bool> occupied_three{true,true,true,false,false};
    auto farthest=session::choose_start_point(more,occupied_three,random,base);
    assert(farthest && *farthest==4);
    std::vector<bool> no_occupied(more.size(),false);
    const auto first_random=expected.ranged(0,4);
    auto first_ai=session::choose_start_point(more,no_occupied,random,base);
    assert(first_ai && *first_ai==static_cast<std::size_t>(first_random));
    assert(random.table()==expected.table() && random.first()==expected.first());
    const auto before_failed=random.table();
    assert(!session::choose_start_point(points,used,random,base));
    assert(random.table()==before_failed);
    used={true,false,false,false};
    const std::vector<session::StartPoint> invalid{{-32768,0},{32767,0},{0,0},{1,0}};
    assert(!session::choose_start_point(invalid,used,random,base));
    assert(!used[1] && !used[2] && !used[3]);

    sim::Random finish_rng(451), finish_expected(451);
    assert(session::finish_starting_unit_generation(finish_rng)==finish_expected.ranged(0,65535));
    assert(finish_rng.state().table==finish_expected.state().table);
    sim::Random choice_rng(789);
    const auto choice_before=choice_rng.table();
    assert(!session::choose_starting_unit(100,0,2,3,choice_rng));
    assert(!session::choose_starting_unit(100,30,2,0,choice_rng));
    assert(choice_rng.table()==choice_before);
    const auto vehicle_choice=session::choose_starting_unit(100,34,1,1,choice_rng);
    assert(vehicle_choice && vehicle_choice->group==session::StartingUnitGroup::Vehicle && vehicle_choice->index==0);
    const auto infantry_choice=session::choose_starting_unit(100,33,1,1,choice_rng);
    assert(infantry_choice && infantry_choice->group==session::StartingUnitGroup::Infantry && infantry_choice->index==0);
    assert(choice_rng.table()==choice_before);

    using session::StartingUnitType;
    {
        const std::vector<StartingUnitType> candidates{
            {true,1,1,90,true,false},{true,1,1,30,false,false}};
        sim::Random plan_rng(11);
        const auto initial_state=plan_rng.table();
        auto planned=session::generate_starting_units(120,1,1,candidates,plan_rng,
            [](std::size_t index,sim::Random&) -> std::optional<session::UnitAttemptResult> {
                return session::UnitAttemptResult{true,{static_cast<std::int16_t>(index+4),8}};
            });
        assert(planned && planned->placed.size()==2 && planned->remaining_budget==0 &&
               planned->failed_placements==0 && planned->placed[0].type_index==0 &&
               planned->placed[1].type_index==1 && plan_rng.table()==initial_state);
        int tries=0;
        auto blocked=session::generate_starting_units(120,1,1,candidates,plan_rng,
            [&](std::size_t,sim::Random&) -> std::optional<session::UnitAttemptResult> {
                ++tries;return session::UnitAttemptResult{false,{0,0}};
            });
        assert(blocked && blocked->placed.empty() && blocked->failed_placements==20 &&
               blocked->remaining_budget==120 && tries==20);
        const auto before_unresolved=plan_rng.table();
        const auto unresolved=session::generate_starting_units(120,1,1,candidates,plan_rng,
            [](std::size_t,sim::Random& random) -> std::optional<session::UnitAttemptResult> {
                random.next();return std::nullopt;
            });
        assert(!unresolved && plan_rng.table()==before_unresolved);
        assert(!session::generate_starting_units(120,1,3,candidates,plan_rng,
            [](std::size_t,sim::Random&) -> std::optional<session::UnitAttemptResult> {
                return session::UnitAttemptResult{true,{0,0}};
            }));
    }

    const std::vector<StartingUnitType> unit_types{
        {true,1,1,100,false,std::nullopt}, {true,3,1,201,false,std::nullopt},
        {true,1,1,3000,true,true}, {false,std::nullopt,std::nullopt,std::nullopt,false,std::nullopt}};
    assert(session::starting_unit_budget(10,1,1,unit_types)==1510);
    assert(session::starting_unit_budget(0,1,1,{})==0);
    assert(session::starting_unit_budget(10,0,1,unit_types)==0);
    auto incomplete=unit_types;
    incomplete[0].cost.reset();
    assert(!session::starting_unit_budget(10,1,1,incomplete));
    assert(!session::starting_unit_budget(2147483647,1,1,unit_types));

    // Resolve the original BaseUnit order and ownership from each edition.
    for (const auto variant : {fs::GameVariant::RedAlert2,fs::GameVariant::YurisRevenge}) {
        fs::MixFileSystem source(root);
        source.bootstrap(variant);
        const auto base_bytes=source.read_file("RULES.INI"); assert(base_bytes);
        rules::IniFile rules;
        rules.parse(std::string_view(reinterpret_cast<const char*>(base_bytes->data()),base_bytes->size()));
        rules::IniFile overlay;
        if (variant==fs::GameVariant::YurisRevenge) {
            const auto data=source.read_file("RULESMD.INI"); assert(data);
            overlay.parse(std::string_view(reinterpret_cast<const char*>(data->data()),data->size()));
        }
        const auto* countries=overlay.find_section("Countries");
        if (!countries) countries=rules.find_section("Countries");
        assert(countries && countries->entries.size()<=32);
        auto names=rules.read_list("General","BaseUnit");
        if (overlay.find("General","BaseUnit")) names=overlay.read_list("General","BaseUnit");
        std::vector<session::BaseUnitCandidate> candidates;
        for (const auto& name : names) {
            auto owners=rules.read_list(name,"Owner");
            if (overlay.find(name,"Owner")) owners=overlay.read_list(name,"Owner");
            std::uint32_t mask=0;
            for (std::size_t i=0;i<countries->entries.size();++i)
                if (std::find(owners.begin(),owners.end(),countries->entries[i].value)!=owners.end()) mask|=std::uint32_t{1}<<i;
            candidates.push_back({name,mask});
        }
        auto for_country=[&](const char* name) {
            const auto c=std::find_if(countries->entries.begin(),countries->entries.end(),
                                      [name](const auto& entry){return entry.value==name;});
            assert(c!=countries->entries.end());
            return session::select_base_unit(candidates,static_cast<unsigned>(c-countries->entries.begin()));
        };
        const auto american=std::find_if(countries->entries.begin(),countries->entries.end(),
                                          [](const auto& entry){return entry.value=="Americans";});
        const auto russian=std::find_if(countries->entries.begin(),countries->entries.end(),
                                         [](const auto& entry){return entry.value=="Russians";});
        assert(american!=countries->entries.end() && russian!=countries->entries.end());
        const auto american_index=static_cast<unsigned>(american-countries->entries.begin());
        const auto russian_index=static_cast<unsigned>(russian-countries->entries.begin());
        const auto e1_cost=overlay.read_integer("E1","Cost",rules.read_integer("E1","Cost",-1));
        const auto e2_cost=overlay.read_integer("E2","Cost",rules.read_integer("E2","Cost",-1));
        assert(e1_cost==200 && e2_cost==100);
        const std::vector<StartingUnitType> infantry{
            {true,std::uint32_t{1}<<american_index,1,e1_cost,false,std::nullopt},
            {true,std::uint32_t{1}<<russian_index,1,e2_cost,false,std::nullopt}};
        assert(session::starting_unit_budget(10,1,std::uint32_t{1}<<american_index,infantry)==2000);
        assert(session::starting_unit_budget(10,1,(std::uint32_t{1}<<american_index)|
                                             (std::uint32_t{1}<<russian_index),infantry)==1500);
        const auto allied=for_country("Americans"), soviet=for_country("Russians");
        assert(allied && candidates[*allied].type=="AMCV");
        assert(soviet && candidates[*soviet].type=="SMCV");
        if (variant==fs::GameVariant::YurisRevenge) {
            const auto yuri_unit=for_country("YuriCountry");
            assert(yuri_unit && candidates[*yuri_unit].type=="PCV");
        }
        assert(!session::select_base_unit(candidates,32));
        candidates[0].owner_mask.reset();
        assert(!session::select_base_unit(candidates,0));
    }

    // Real original standard map points, read through the base MIX filesystem.
    fs::MixFileSystem files(root);
    files.mount_language(fs::GameVariant::RedAlert2);
    files.bootstrap(fs::GameVariant::RedAlert2);
    files.mount("MULTI.MIX");
    auto bytes=files.read_file("MP02T2.MAP"); assert(bytes);
    rules::IniFile ini;
    ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
    std::vector<session::StartPoint> real;
    for (int i=0;i<8;++i) {
        const auto* entry=ini.find("Waypoints",std::to_string(i));
        if (!entry) break;
        const auto value=rules::IniFile::integer_value(entry->value);
        assert(value && *value>=0);
        real.push_back({static_cast<std::int16_t>(*value%1000),static_cast<std::int16_t>(*value/1000)});
    }
    assert(real.size()>=2 && real.size()<=8);
    auto rectangle=[&](const char* key) {
        const auto fields=ini.read_list("Map",key);
        assert(fields.size()==4);
        std::int32_t values[4];
        for (std::size_t i=0;i<4;++i) {
            const auto value=rules::IniFile::integer_value(fields[i]);
            assert(value); values[i]=*value;
        }
        return session::MapRectangle{values[0],values[1],values[2],values[3]};
    };
    const auto geometry=session::resolve_placement_geometry(rectangle("Size"),rectangle("LocalSize"));
    assert(geometry);
    // Build a complete fixture with real map dimensions. These height and
    // ramp values are fixtures, not decoded terrain from the map.
    sim::TerrainCells terrain;
    for (int y=0;y<512;++y) for (int x=0;x<512;++x) {
        const session::StartPoint point{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
        const auto allocated=session::placement_cell_in_map(point,session::PlacementEdition::Base,*geometry,std::nullopt);
        assert(allocated);
        if (*allocated) {
            sim::TerrainCell cell;
            cell.cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
            cell.height_level=0; cell.ramp_index=0;
            terrain.push_back(cell);
        }
    }
    auto grid=session::PlacementTerrain::create(*geometry,terrain); assert(grid);
    for (const auto point:real) {
        const auto present=grid->in_map(point,session::PlacementEdition::Base);
        assert(present && *present);
        const auto actual=grid->in_map(point,session::PlacementEdition::Yuri);
        const auto expected=session::placement_cell_in_map(point,session::PlacementEdition::Yuri,*geometry,session::PlacementCell{0,0});
        assert(actual==expected);
    }
    terrain.front().height_level.reset();
    grid=session::PlacementTerrain::create(*geometry,terrain); assert(grid);
    const session::StartPoint unknown{static_cast<std::int16_t>(terrain.front().cell.x),static_cast<std::int16_t>(terrain.front().cell.y)};
    assert(!grid->in_map(unknown,session::PlacementEdition::Yuri));
    // The engine uses a linear index, so x+512 aliases the next grid row.
    if (unknown.y>0) {
        const session::StartPoint alias{static_cast<std::int16_t>(unknown.x+512),static_cast<std::int16_t>(unknown.y-1)};
        assert(!grid->in_map(alias,session::PlacementEdition::Yuri));
    }
    auto duplicate=terrain;
    duplicate.back()=duplicate.front();
    assert(!session::PlacementTerrain::create(*geometry,duplicate));
    terrain.push_back(terrain.front());
    assert(!session::PlacementTerrain::create(*geometry,terrain));
    sim::Random random_base(42), random_yuri(42);
    std::vector<bool> used_base(real.size(),false),used_yuri(real.size(),false);
    for (std::size_t i=0;i<real.size();++i) {
        const auto a=session::choose_start_point(real,used_base,random_base,base);
        const auto b=session::choose_start_point(real,used_yuri,random_yuri,yuri);
        assert(a && b && *a==*b);
    }
    assert(random_base.table()==random_yuri.table());

    // Staged phase test uses real map points and real base rules. The world
    // and placement outcomes below are supplied fixtures, not game behavior.
    const auto rules_bytes=files.read_file("RULES.INI"); assert(rules_bytes);
    rules::IniFile base_rules;
    base_rules.parse(std::string_view(reinterpret_cast<const char*>(rules_bytes->data()),rules_bytes->size()));
    const auto* registry=base_rules.find_section("Countries"); assert(registry);
    auto index_of=[&](const char* name) {
        const auto it=std::find_if(registry->entries.begin(),registry->entries.end(),
                                    [name](const auto& item){return item.value==name;});
        assert(it!=registry->entries.end());
        return static_cast<unsigned>(it-registry->entries.begin());
    };
    const auto america=index_of("Americans"), russia=index_of("Russians");
    auto owner_bits=[&](const char* type) {
        const auto owners=base_rules.read_list(type,"Owner");
        std::uint32_t bits=0;
        for (std::size_t i=0;i<registry->entries.size();++i)
            if (std::find(owners.begin(),owners.end(),registry->entries[i].value)!=owners.end())
                bits|=std::uint32_t{1}<<i;
        return bits;
    };
    session::BattlePlanInput input{5000,1,1,true,
        {{0,america,std::uint32_t{1}<<america,0,1,1},
         {1,russia,std::uint32_t{1}<<russia,1,1,1}},
        {real[0],real[1]},
        {{"AMCV",owner_bits("AMCV")},{"SMCV",owner_bits("SMCV")}},
        {{true,owner_bits("E1"),1,base_rules.read_integer("E1","Cost",-1),false,std::nullopt},
         {true,owner_bits("E2"),1,base_rules.read_integer("E2","Cost",-1),false,std::nullopt}}};
    std::vector<std::string> events;
    auto prepare=[&](const std::vector<session::BattleHousePlan>& houses,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {
        events.push_back("prepare");state.next();
        session::PreparedBattleWorld world;
        for (const auto& h:houses) {
            world.active_slots.push_back(h.input.slot);world.starts.push_back(h.start);
            world.cash.push_back(h.initial_values.cash);
        }
        world.resolved_types_unchanged=true;
        return world;
    };
    auto place=[&](std::size_t house,bool base_unit,std::size_t,session::StartPoint start,sim::Random&)
        -> std::optional<session::UnitAttemptResult> {
        events.push_back(std::string(base_unit ? "base" : "unit")+std::to_string(house));
        return session::UnitAttemptResult{true,start};
    };
    auto finish_house=[&](std::size_t house,std::int32_t cash,sim::Random& state)
        -> std::optional<std::int32_t> {
        events.push_back("house"+std::to_string(house));state.next();return cash;
    };
    auto finish_world=[&](const std::vector<session::BattleHousePlan>& houses,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {
        events.push_back("finish");state.next();
        session::PreparedBattleWorld world;
        for (const auto& h:houses) {
            world.active_slots.push_back(h.input.slot);world.starts.push_back(h.start);
            world.cash.push_back(h.cash_after_units);
        }
        world.resolved_types_unchanged=true;
        world.staged_units_unchanged=true;
        return world;
    };
    sim::Random staged_rng(951), staged_expected(951);
    const auto planned=session::plan_battle(input,staged_rng,prepare,place,finish_house,finish_world);
    assert(planned && planned->unit_budget==150 && planned->houses.size()==2);
    assert(planned->houses[0].base_unit && planned->houses[0].base_unit->type_index==0 &&
           planned->houses[1].base_unit && planned->houses[1].base_unit->type_index==1);
    assert(planned->houses[0].extra_units.size()==1 && planned->houses[1].extra_units.size()==2);
    assert(planned->houses[0].extra_unit_mission==session::StartingMission::Guard &&
           planned->houses[1].extra_unit_mission==session::StartingMission::AreaGuard);
    assert(planned->houses[0].cash_after_units==5000 && planned->houses[1].cash_after_units==5000 &&
           planned->houses[0].cash_after_world==5000 && planned->houses[1].cash_after_world==5000);
    assert((events==std::vector<std::string>{"prepare","base0","unit0","house0",
                                             "base1","unit1","unit1","house1","finish"}));
    staged_expected.next();staged_expected.next();staged_expected.next();
    session::finish_starting_unit_generation(staged_expected);staged_expected.next();
    assert(staged_rng.state().table==staged_expected.state().table &&
           staged_rng.state().first==staged_expected.state().first &&
           staged_rng.state().second==staged_expected.state().second);
    const auto saved_state=staged_rng.state();
    auto unknown_world=[](const std::vector<session::BattleHousePlan>&,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {state.next();return std::nullopt;};
    assert(!session::plan_battle(input,staged_rng,unknown_world,place,finish_house,finish_world));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto unknown_place=[](std::size_t,bool,std::size_t,session::StartPoint,sim::Random& state)
        -> std::optional<session::UnitAttemptResult> {state.next();return std::nullopt;};
    assert(!session::plan_battle(input,staged_rng,prepare,unknown_place,finish_house,finish_world));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto unknown_house=[](std::size_t,std::int32_t,sim::Random& state)
        -> std::optional<std::int32_t> {state.next();return std::nullopt;};
    assert(!session::plan_battle(input,staged_rng,prepare,place,unknown_house,finish_world));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto unknown_finish=[](const std::vector<session::BattleHousePlan>&,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {state.next();return std::nullopt;};
    assert(!session::plan_battle(input,staged_rng,prepare,place,finish_house,unknown_finish));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto wrong_starts=[&](const std::vector<session::BattleHousePlan>& houses,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {
        auto world=prepare(houses,state);assert(world);world->starts[0].x++;
        return world;
    };
    assert(!session::plan_battle(input,staged_rng,wrong_starts,place,finish_house,finish_world));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto changed_units=[&](const std::vector<session::BattleHousePlan>& houses,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {
        auto world=finish_world(houses,state);assert(world);world->staged_units_unchanged=false;
        return world;
    };
    assert(!session::plan_battle(input,staged_rng,prepare,place,finish_house,changed_units));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);
    auto unknown_types=[&](const std::vector<session::BattleHousePlan>& houses,sim::Random& state)
        -> std::optional<session::PreparedBattleWorld> {
        auto world=prepare(houses,state);assert(world);world->resolved_types_unchanged=false;
        return world;
    };
    assert(!session::plan_battle(input,staged_rng,unknown_types,place,finish_house,finish_world));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first &&
           staged_rng.state().second==saved_state.second);
    assert(!session::plan_battle(input,staged_rng,prepare,place,finish_house,{}));
    assert(staged_rng.state().table==saved_state.table && staged_rng.state().first==saved_state.first && staged_rng.state().second==saved_state.second);

    std::cout<<"original start selection and real map waypoints passed\n";
}
