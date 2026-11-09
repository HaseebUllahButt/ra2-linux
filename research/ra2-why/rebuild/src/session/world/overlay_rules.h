#pragma once
#include "rules/ini_file.h"
#include <cstdint>
#include <optional>
#include <string>

namespace ra2::session::world {
// Checked fields in one base OverlayType constructor/INI-reader slice.
// CellAnim, image loading, and all other type fields remain separate.
struct OverlayRuleFields {
    std::int32_t land=0; // OverlayType +268.
    bool wall=false, tiberium=false, crate=false; // +278, +279, +27A.
};
// Constructor defaults at 005DC5A0, before any INI reader.
OverlayRuleFields base_overlay_type_constructor() noexcept;
// Apply one actual INI layer after its ObjectType reader succeeds. An absent
// section performs no writes. Unsupported authored values return no result.
// Call for every real layer in original order; this is not a live type load.
std::optional<OverlayRuleFields> read_base_overlay_rule_slice(
    const rules::IniFile&,const std::string& type,OverlayRuleFields before);
}
