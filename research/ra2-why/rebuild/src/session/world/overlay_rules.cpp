#include "overlay_rules.h"
#include <array>
#include <string_view>
namespace ra2::session::world {
namespace {
bool equal_ascii(std::string_view a,std::string_view b) {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i) {
        char c=a[i]; if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        if (c!=b[i]) return false;
    }
    return true;
}
std::optional<bool> boolean(std::string_view value) {
    if (equal_ascii(value,"yes") || equal_ascii(value,"true") || value=="1") return true;
    if (equal_ascii(value,"no") || equal_ascii(value,"false") || value=="0") return false;
    return {};
}
}
OverlayRuleFields base_overlay_type_constructor() noexcept { return {}; }
std::optional<OverlayRuleFields> read_base_overlay_rule_slice(
    const rules::IniFile& ini,const std::string& type,OverlayRuleFields before) {
    if (type.empty()) return {};
    if (!ini.find_section(type)) return before;
    if (const auto* value=ini.find(type,"Land")) {
        constexpr std::array<const char*,12> names={"clear","road","water","rock","wall","tiberium",
                                                    "beach","rough","ice","railroad","tunnel","weeds"};
        before.land=-1;
        for (std::size_t i=0;i<names.size();++i)
            if (equal_ascii(value->value,names[i])) { before.land=static_cast<std::int32_t>(i); break; }
    }
    for (const auto& item:{std::pair<const char*,bool*>{"Wall",&before.wall},
                           {"Tiberium",&before.tiberium},{"Crate",&before.crate}}) {
        if (const auto* value=ini.find(type,item.first)) {
            const auto parsed=boolean(value->value);
            if (!parsed) return {};
            *item.second=*parsed;
        }
    }
    // 005DCC13 maps constructor/default Clear to Tiberium after the reader.
    if (before.tiberium && before.land==0) before.land=5;
    return before;
}
}
