#include "walk_motion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace ra2::sim {
OriginalWalkMath OriginalWalkMath::from_executable(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("cannot read original walk tables: "+path);
    // These original .text/.data sections have matching file offsets and RVAs
    // (same assumption as distance.cpp). Check helper signatures so another
    // executable cannot silently supply this layout.
    using Signature=std::vector<unsigned char>;
    const std::uint32_t sine_helper=0x4ba480,cosine_helper=0x4ba4d0;
    const std::uint32_t arctangent_helper=0x4ba600,truncation_helper=0x77f110;
    const std::uint32_t trig_table_address=0x801dc4,atan_table_address=0x813df4;
    const Signature sine_signature{0xdd,0x44,0x24,0x04,0xd8,0x0d,0x48,0x80,0x7d,0x00};
    const Signature cosine_signature{0xdd,0x44,0x24,0x04,0xd8,0x0d,0x4c,0x80,0x7d,0x00};
    const Signature arctangent_signature{0x51,0xdd,0x44,0x24,0x10,0xd9,0x5c,0x24,0x00};
    const Signature truncation_signature{0x83,0xec,0x0c,0xd9,0x3c,0x24};
    auto matches=[&](std::uint32_t helper,const std::vector<unsigned char>& signature){
        std::vector<unsigned char> actual(signature.size());
        file.clear();
        file.seekg(static_cast<std::streamoff>(helper-0x400000));
        file.read(reinterpret_cast<char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
        return static_cast<bool>(file) && actual==signature;
    };
    if (!matches(sine_helper,sine_signature) ||
        !matches(cosine_helper,cosine_signature) ||
        !matches(arctangent_helper,arctangent_signature) ||
        !matches(truncation_helper,truncation_signature))
        throw std::runtime_error("unsupported original executable walk helper layout");
    auto read_floats=[&](std::uint32_t table,auto& entries){
        file.clear();
        file.seekg(static_cast<std::streamoff>(table-0x400000));
        for (auto& entry:entries) {
            unsigned char b[4];
            file.read(reinterpret_cast<char*>(b),4);
            if (!file) throw std::runtime_error("truncated original walk table");
            std::uint32_t bits=static_cast<std::uint32_t>(b[0])|static_cast<std::uint32_t>(b[1])<<8|
                static_cast<std::uint32_t>(b[2])<<16|static_cast<std::uint32_t>(b[3])<<24;
            std::memcpy(&entry,&bits,4);
        }
    };
    OriginalWalkMath out;
    read_floats(trig_table_address,out.trig_table_);
    read_floats(atan_table_address,out.atan_table_);
    auto read_double=[&](std::uint32_t address){
        unsigned char b[8];
        file.clear();
        file.seekg(static_cast<std::streamoff>(address-0x400000));
        file.read(reinterpret_cast<char*>(b),8);
        if (!file) throw std::runtime_error("truncated original walk constant");
        double value;
        std::memcpy(&value,b,8);
        return value;
    };
    auto read_float=[&](std::uint32_t address){
        unsigned char b[4];
        file.clear();
        file.seekg(static_cast<std::streamoff>(address-0x400000));
        file.read(reinterpret_cast<char*>(b),4);
        if (!file) throw std::runtime_error("truncated original walk constant");
        float value;
        std::memcpy(&value,b,4);
        return value;
    };
    out.rad_scale_=read_double(0x79b620);          // 0071F3C9 fmull
    out.angle_scale_=read_double(0x79b628);        // 0071F367 fmull
    out.angle_offset_=read_double(0x79b630);       // 0071F35E fsubl
    out.pi_=read_double(0x79d158);                 // 004BA697 fsubrl
    out.trig_scale_=read_float(0x7d8048);          // 004BA484 fmuls
    out.atan_divisor_=read_float(0x817df8);        // 004BA660 fdivs
    out.positive_half_pi_=read_float(0x7a0f8c);    // 004BA64C flds
    out.negative_half_pi_=read_float(0x7a0f90);    // 004BA654 flds
    // Table shape checks: sine peaks at 0x800 and 0x1800, arctangent starts at zero.
    if (out.trig_table_.at(0x800)!=1.0f || out.trig_table_.at(0x1800)!=-1.0f ||
        out.atan_table_.at(0)!=0.0f)
        throw std::runtime_error("original walk tables do not match the verified shape");
    return out;
}
std::int64_t OriginalWalkMath::truncate_to_integer(long double value) const {
    // 0077F110: fistpll with the saved x87 rounding control, which truncates.
    if (!(value>=-9223372036854775808.0L && value<9223372036854775808.0L))
        throw std::range_error("0077F110 input outside the signed 64-bit domain");
    return static_cast<std::int64_t>(value);
}
float OriginalWalkMath::sine(double radians) const {
    // 004BA480: fldl argument; fmuls 0x7D8048; call 0077F110.
    const std::int64_t n=truncate_to_integer(
        static_cast<long double>(radians)*static_cast<long double>(trig_scale_));
    std::int32_t index=static_cast<std::int32_t>(n);
    const bool odd=(n&1)!=0;                       // 004BA48F mov %al,%cl
    index-=index<0?-1:0;                           // 004BA491 cltd; 004BA492 sub %edx,%eax
    index>>=1;                                     // 004BA497 sar $1,%eax
    index&=static_cast<std::int32_t>(0x80001fffu); // 004BA499 and $0x80001fff,%eax
    if (index<0) {                                 // 004BA49E jns
        index=((index-1)|static_cast<std::int32_t>(0xffffe000u))+1;
        if (index<0) index+=0x2000;                // 004BA4A9 add $0x2000,%eax
    }
    if (odd && index<0x1fff) ++index;              // 004BA4AE..004BA4B9
    return trig_table_.at(static_cast<std::size_t>(index));
}
float OriginalWalkMath::cosine(double radians) const {
    // 004BA4D0: same index math as the sine, then a quarter turn offset.
    const std::int64_t n=truncate_to_integer(
        static_cast<long double>(radians)*static_cast<long double>(trig_scale_));
    std::int32_t index=static_cast<std::int32_t>(n);
    const bool odd=(n&1)!=0;
    index-=index<0?-1:0;
    index>>=1;
    index&=static_cast<std::int32_t>(0x80001fffu);
    if (index<0) {
        index=((index-1)|static_cast<std::int32_t>(0xffffe000u))+1;
        index+=index<0?0x2800:0x800;               // 004BA4F9 / 004BA500
    } else {
        index+=0x800;                              // 004BA500
    }
    if (odd && index<0x27ff) ++index;              // 004BA505..004BA510
    return trig_table_.at(static_cast<std::size_t>(index));
}
long double OriginalWalkMath::arctangent(double rise,double run) const {
    // 004BA600. Call order at 0071F34C/0071F356: first argument is
    // current.y - destination.y, second is destination.x - current.x.
    // Inside: A = second argument (run), B = first argument (rise).
    const float a=static_cast<float>(run);         // 004BA609 fldl 0x8(%esp)
    const float b=static_cast<float>(rise);        // 004BA601 fldl 0x10(%esp)
    if (a==0.0f) {                                 // 004BA615/004BA621
        if (b==0.0f) return 0.0f;                  // 004BA633
        return b<0.0f                                  // 004BA647 test $0x41
            ? static_cast<long double>(negative_half_pi_)  // 004BA654
            : static_cast<long double>(positive_half_pi_); // 004BA64C
    }
    long double quotient=static_cast<long double>(b)/static_cast<long double>(a); // 004BA65C
    quotient/=static_cast<long double>(atan_divisor_);                             // 004BA660
    std::int32_t index=static_cast<std::int32_t>(truncate_to_integer(quotient));   // 004BA666
    index=index<0?-index:index;                    // 004BA66B cltd; xor; sub
    // 004BA670 cmp $0x1001; 004BA675 table; 004BA680 half pi.
    long double angle=index<0x1001
        ? static_cast<long double>(atan_table_.at(static_cast<std::size_t>(index)))
        : static_cast<long double>(positive_half_pi_);
    if (a<0.0f) angle=static_cast<long double>(pi_)-angle;  // 004BA697 fsubrl 0x79D158
    if (b<0.0f) angle=-angle;                               // 004BA6AE fchs
    return angle;
}
OriginalWalkMath::Step OriginalWalkMath::step(Coord current,Coord destination,int speed) const {
    // 0071F33E..0071F356: destination.x - current.x and current.y - destination.y.
    const double dx=static_cast<double>(static_cast<std::int64_t>(destination.x)-current.x);
    const double dy=static_cast<double>(static_cast<std::int64_t>(current.y)-destination.y);
    // 0071F359 call 004BA600; 0071F35E fsubl 0x79B630; 0071F367 fmull 0x79B628;
    // 0071F36D call 0077F110; 0071F375 mov %ax,%si (low 16 bits).
    const std::int16_t facing=static_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::uint64_t>(truncate_to_integer(
            (arctangent(dy,dx)-static_cast<long double>(angle_offset_))*
            static_cast<long double>(angle_scale_)))));
    // 0071F3B8 movswl %si,%ecx; 0071F3BB sub $0x3FFF,%ecx; 0071F3C5 fildl;
    // 0071F3C9 fmull 0x79B620; 0071F3CF fstpl (rounded to double).
    const std::int32_t turned=static_cast<std::int32_t>(facing)-0x3FFF;
    const double radians=static_cast<double>(
        static_cast<long double>(turned)*static_cast<long double>(rad_scale_));
    // 0071F3D3 fildl speed; 0071F3ED call 004BA480; 0071F3F2 fmull speed;
    // 0071F3F6 fsubrl current.y; 0071F3FA call 0077F110.
    const float sine_value=sine(radians);
    const std::int32_t new_y=static_cast<std::int32_t>(truncate_to_integer(
        static_cast<long double>(current.y)-
        static_cast<long double>(speed)*static_cast<long double>(sine_value)));
    // 0071F407 call 004BA4D0; 0071F40C fmull speed; 0071F413 fiaddl current.x;
    // 0071F417 call 0077F110.
    const float cosine_value=cosine(radians);
    const std::int32_t new_x=static_cast<std::int32_t>(truncate_to_integer(
        static_cast<long double>(speed)*static_cast<long double>(cosine_value)+
        static_cast<long double>(current.x)));
    // 0071F3B5: z (this+0x90) passes through unchanged on the flat branch.
    return {{new_x,new_y,current.z},facing};
}
}
