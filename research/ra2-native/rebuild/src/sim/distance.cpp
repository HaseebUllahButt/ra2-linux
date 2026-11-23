#include "distance.h"
#include "fp.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace ra2::sim {
OriginalDistance OriginalDistance::from_executable(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("cannot read original distance table: "+path);
    // These original .text/.data sections have matching file offsets and RVAs.
    // Check the helper signature so another executable cannot silently supply this layout.
    struct Layout { std::uint32_t helper,table; unsigned char signature[10]; };
    constexpr Layout layouts[]={
        {0x4ba410,0x817dfc,{0xdd,0x44,0x24,0x04,0xd8,0x15,0x38,0xa7,0x79,0x00}},
        {0x4cac40,0x8650bc,{0xdd,0x44,0x24,0x04,0xd8,0x15,0x48,0x17,0x7e,0x00}}
    };
    std::uint32_t table=0;
    for (const auto& layout:layouts) {
        unsigned char actual[10]{};
        file.clear(); file.seekg(layout.helper-0x400000);
        file.read(reinterpret_cast<char*>(actual),sizeof(actual));
        if (file && std::equal(std::begin(layout.signature),std::end(layout.signature),actual)) {
            table=layout.table; break;
        }
    }
    if (!table) throw std::runtime_error("unsupported original executable distance helper layout");
    file.clear(); file.seekg(table-0x400000);
    OriginalDistance out;
    for (auto& entry:out.table_) {
        unsigned char b[4]; file.read(reinterpret_cast<char*>(b),4);
        if (!file) throw std::runtime_error("truncated original distance table");
        entry=static_cast<std::uint32_t>(b[0]) | static_cast<std::uint32_t>(b[1])<<8 |
              static_cast<std::uint32_t>(b[2])<<16 | static_cast<std::uint32_t>(b[3])<<24;
    }
    return out;
}
float OriginalDistance::square_root(double squared) const {
    if (!squared) return 0;
    if (!std::isfinite(squared) || squared<1 || squared>std::numeric_limits<float>::max())
        throw std::range_error("004CAC40 input outside verified nonnegative integer-distance domain");
    float single=static_cast<float>(squared);
    if (static_cast<double>(single)>squared) single=std::nextafter(single,0.0f);
    std::uint32_t bits; std::memcpy(&bits,&single,4);
    int exponent=static_cast<int>(bits>>23)-127;
    std::uint32_t mantissa=bits&0x7fffffu;
    if (exponent&1) mantissa|=0x800000u;
    // Positive inputs >=1 give a nonnegative exponent, avoiding implementation-defined shifts.
    const auto result_bits=table_.at(mantissa>>10)+(static_cast<std::uint32_t>(exponent/2+127)<<23);
    float result; std::memcpy(&result,&result_bits,4);
    if (!std::isfinite(result) || result<0) throw std::runtime_error("invalid original distance table result");
    return result;
}
int OriginalDistance::between(Coord from,Coord to,bool planar) const {
    auto delta=[](int a,int b){
        auto d=static_cast<std::int64_t>(a)-b;
        if (d<std::numeric_limits<int>::min() || d>std::numeric_limits<int>::max())
            throw std::range_error("original coordinate delta overflow");
        return static_cast<double>(d);
    };
    const auto x=delta(from.x,to.x), y=delta(from.y,to.y), z=planar?0.0:delta(from.z,to.z);
    // 0070CFF8..0070D009: add x squared and z squared, then add y squared.
    const auto squared=fp::add(fp::add(fp::multiply(x,x),fp::multiply(z,z)),fp::multiply(y,y));
    const auto result=square_root(squared);
    if (static_cast<double>(result)>std::numeric_limits<int>::max())
        throw std::range_error("original distance integer conversion overflow");
    return static_cast<int>(result);
}
}
