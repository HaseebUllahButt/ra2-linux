#include "sim.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ra2::sim {
namespace {
std::uint32_t asr16(std::uint32_t n) {
    return (n >> 16) | ((n & 0x80000000u) ? 0xffff0000u : 0u);
}
}
Random::Random(std::uint32_t seed) {
    // Base game.exe 007EE4AC/007EE4FC; identical expansion bytes at 00839644/00839694.
    constexpr std::uint32_t a[]={0xbaa96887,0x1e17d32c,0x03bcdc3c,0x0f33d1b2};
    constexpr std::uint32_t b[]={0x4b0f3b58,0xe874f0c3,0x6955c5a6,0x55a7ca46};
    for (std::uint32_t i=0;i<table_.size();++i) {
        std::uint32_t left=seed, right=i;
        for (unsigned j=0;j<4;++j) {
            const auto n=a[j]^right;
            const auto high=asr16(n), low=n&0xffffu;
            const auto mixed=~(high*high)+low*low;
            const auto rotated=asr16(mixed)|(mixed<<16);
            const auto out=((rotated^b[j])+high*low)^left;
            left=right; right=out;
        }
        table_[i]=right;
    }
}
std::uint32_t Random::next() {
    auto result=table_[first_]^table_[second_];
    table_[first_]=result;
    first_=(first_+1)%250; second_=(second_+1)%250;
    return result;
}
std::int32_t Random::ranged(std::int32_t low,std::int32_t high) {
    if (low==high) return low;
    if (high<low) std::swap(low,high);
    const auto span=static_cast<std::uint64_t>(static_cast<std::int64_t>(high)-low);
    // 0065C7E0 compares signed spans and uses x86 masked shifts.
    // Reject the ambiguous overflow path instead of changing its distribution.
    if (span>static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
        throw std::range_error("0065C7E0: signed range overflow is unsupported");
    std::uint32_t mask=static_cast<std::uint32_t>(span);
    mask|=mask>>1; mask|=mask>>2; mask|=mask>>4; mask|=mask>>8; mask|=mask>>16;
    std::uint32_t value;
    do { value=next()&mask; } while (value>span);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(low)+value);
}
int factory_stage_frames(int frames) {
    return std::clamp(frames/54,1,255); // 004C9FB0; not the unresolved 006F47A0 formula.
}
}
