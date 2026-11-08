#include "session.h"
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
    sim::Random random_base(42), random_yuri(42);
    std::vector<bool> used_base(real.size(),false),used_yuri(real.size(),false);
    for (std::size_t i=0;i<real.size();++i) {
        const auto a=session::choose_start_point(real,used_base,random_base,base);
        const auto b=session::choose_start_point(real,used_yuri,random_yuri,yuri);
        assert(a && b && *a==*b);
    }
    assert(random_base.table()==random_yuri.table());
    std::cout<<"original start selection and real map waypoints passed\n";
}
