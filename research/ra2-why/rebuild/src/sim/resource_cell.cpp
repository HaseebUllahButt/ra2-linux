#include "resource_cell.h"
#include <limits>

namespace ra2::sim {
namespace {
struct Stop { ResourceCellStatus status; const char* detail; };
template<class T> T need(const std::optional<T>& value,const char* detail) {
    if (!value) throw Stop{ResourceCellStatus::NeedsInput,detail};
    return *value;
}
std::int32_t signed_word(std::uint32_t value) {
    return value<=0x7fffffffu ? static_cast<std::int32_t>(value)
        : -1-static_cast<std::int32_t>(0xffffffffu-value);
}
std::int32_t add(std::int32_t a,std::int32_t b) {
    return signed_word(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));
}
int lookup(const ResourceOverlay& overlay,const std::optional<std::vector<ResourceType>>& registry) {
    const auto index=need(overlay.index,"Cell overlay +44 is unknown");
    if (index==-1) return -1;
    if (index<0) throw Stop{ResourceCellStatus::Unsupported,"negative OverlayType index would access unknown memory"};
    if (!need(overlay.tiberium,"OverlayType +279 is unknown")) return -1;
    if (!registry) throw Stop{ResourceCellStatus::NeedsInput,"Tiberium registry is unknown"};
    for (const auto& type:*registry) {
        const auto first=need(type.first_overlay,"TiberiumType first overlay index is unknown");
        const auto count=need(type.count,"TiberiumType +E8 is unknown");
        const auto end=add(first,count);
        if (index>=first && index<end)
            return need(type.index,"TiberiumType +98 is unknown");
        if (index>=end) {
            const auto extra=need(type.extra_count,"TiberiumType +EC is unknown");
            if (index<add(end,extra))
                return need(type.index,"TiberiumType +98 is unknown");
        }
    }
    // 005DC0B0 logs a flagged Overlay outside every range, then returns 0.
    return 0;
}
std::int32_t ranged(std::int32_t low,std::int32_t high,ResourceCellResult& out,
                    const std::optional<bool>& disabled) {
    if (low==high) return low;
    if (high<low) { auto swap=low; low=high; high=swap; }
    const auto span=static_cast<std::uint32_t>(high)-static_cast<std::uint32_t>(low);
    if (span==0x7fffffffu) return signed_word(static_cast<std::uint32_t>(low)+0x80000000u);
    if (span>0x7fffffffu)
        throw Stop{ResourceCellStatus::Unsupported,"original signed random range does not terminate"};
    const bool off=need(disabled,"random generator disable byte is unknown");
    if (off) return low;
    if (!out.random) throw Stop{ResourceCellStatus::NeedsInput,"random generator state is unknown"};
    auto& random=*out.random;
    if (random.first>=250 || random.second>=250)
        throw Stop{ResourceCellStatus::Unsupported,"random indexes would access unknown memory"};
    std::uint32_t mask=span;
    mask|=mask>>1; mask|=mask>>2; mask|=mask>>4; mask|=mask>>8; mask|=mask>>16;
    // A broken state can make the original retry forever. Keep the partial
    // state and report the limit instead of blocking the loader.
    for (unsigned n=0;n<1000000;++n) {
        const auto word=random.table[random.first]^random.table[random.second];
        random.table[random.first]=word;
        random.first=(random.first+1)%250;
        random.second=(random.second+1)%250;
        const auto draw=word&mask;
        if (draw<=span) return signed_word(static_cast<std::uint32_t>(low)+draw);
    }
    throw Stop{ResourceCellStatus::Unsupported,"random retry limit reached"};
}
}
ResourceCellResult setup_original_resource_cell(const ResourceCellInput& input) {
    ResourceCellResult out;
    out.overlay=input.cell.index; out.data=input.data; out.random=input.random;
    out.fallback_coordinate=input.fallback_coordinate;
    try {
        const auto type_index=lookup(input.cell,input.registry);
        if (type_index==-1) {
            out.value=0; out.status=ResourceCellStatus::Complete; return out;
        }
        if (!input.registry) throw Stop{ResourceCellStatus::NeedsInput,"Tiberium registry is unknown"};
        if (type_index<0 || static_cast<std::size_t>(type_index)>=input.registry->size())
            throw Stop{ResourceCellStatus::Unsupported,"TiberiumType +98 points outside the registry"};
        const auto& type=(*input.registry)[static_cast<std::size_t>(type_index)];
        if (need(input.randomize,"resource randomize argument is unknown")) {
            const auto address=need(type.first_overlay_address,"TiberiumType +E0 address is unknown");
            const auto count=need(type.count,"TiberiumType +E8 is unknown");
            const auto high=address+(static_cast<std::uint32_t>(count)-1u)*0x288u;
            out.overlay=ranged(signed_word(address),signed_word(high),out,input.random_disabled);
            out.overlay_written=true;
        }
        int matching=0;
        constexpr std::array<int,8> dx={0,1,1,1,0,-1,-1,-1};
        constexpr std::array<int,8> dy={-1,-1,0,1,1,1,0,-1};
        for (unsigned i=0;i<8;++i) {
            if (need(input.neighbor_fallback[i],"neighbor map lookup is unknown")) {
                const auto coordinate=need(input.cell_coordinate,"Cell +24 is unknown");
                const auto x=(coordinate+static_cast<std::uint32_t>(dx[i]))&0xffffu;
                const auto y=((coordinate>>16)+static_cast<std::uint32_t>(dy[i]))&0xffffu;
                const auto destination=x|(y<<16);
                out.fallback_coordinate=destination;
                out.fallback_writes.push_back({i,destination});
            }
            if (lookup(input.neighbors[i],input.registry)==type_index) ++matching;
        }
        const auto divisor=need(type.divisor,"TiberiumType +E4 is unknown");
        if (divisor==0) throw Stop{ResourceCellStatus::Unsupported,"original resource divisor is zero"};
        constexpr std::array<std::uint8_t,9> density={0,1,3,4,6,7,8,10,11};
        // matching is 0..8, so even negative divisors have a nonnegative remainder.
        const auto remainder=matching%divisor;
        const auto data=density[static_cast<std::size_t>(remainder)];
        out.data=data; out.data_written=true;
        const auto value=need(type.value,"TiberiumType +B8 is unknown");
        out.value=signed_word(static_cast<std::uint32_t>(value)*(static_cast<std::uint32_t>(data)+1u));
        out.status=ResourceCellStatus::Complete;
    } catch (const Stop& stop) {
        out.status=stop.status; out.detail=stop.detail;
    }
    return out;
}
}
