#include "overlay_mark.h"

namespace ra2::sim {
namespace {
struct Missing { std::string detail; };
template<class T> T require(const std::optional<T>& value,const char* detail) {
    if (!value) throw Missing{detail};
    return *value;
}
bool root_index(std::int32_t index) { return index==24 || index==25 || index==237 || index==238; }
std::int32_t signed_word(std::uint32_t value) {
    return value<=0x7fffffffu?static_cast<std::int32_t>(value):-1-static_cast<std::int32_t>(0xffffffffu-value);
}
}
OverlayMarkResult dispatch_original_overlay_mark(int mode,
    const OverlayMarkEnvironment& env,const OverlayMarkCallbacks& cb) {
    OverlayMarkResult out;
    const auto effect=[&](OverlayEffectKind kind,std::int32_t value=0,std::int32_t second=0,
                          std::int32_t third=0,int neighbor=-1) {
        const OverlayEffect action{kind,value,second,third,neighbor};
        out.effects.push_back(action);
        if (!cb.effect) throw Missing{"overlay effect callback is missing"};
        return require(cb.effect(action),"overlay effect body is unresolved");
    };
    const auto type=[&]() {
        if (!cb.type) throw Missing{"overlay type callback is missing"};
        return require(cb.type(),"current overlay type is unresolved");
    };
    const auto cell=[&]() {
        if (!cb.cell) throw Missing{"overlay cell callback is missing"};
        return require(cb.cell(),"current overlay cell is unresolved");
    };
    const auto index=[&]() { return require(type().index,"overlay type index is unresolved"); };
    const auto overridden=[&]() { return require(env.placement_override,"overlay placement override is unresolved"); };
    const auto finish=[&]() {
        effect(OverlayEffectKind::TileSetup,-1);
        // Base +74=0,+76=1; expansion +74=0,+81=1, then virtual +F0/+F8.
        effect(OverlayEffectKind::FinishObject);
        effect(OverlayEffectKind::Dispose);
        out.status=OverlayMarkStatus::Applied;
    };
    const auto reject=[&]() { out.status=OverlayMarkStatus::Rejected; };
    try {
        // Parent Object::Mark 005D4160 / 005F5850 runs even for other modes.
        if (!effect(OverlayEffectKind::ParentMark,mode) || (mode!=1 && mode!=3)) {
            reject(); return out;
        }
        effect(OverlayEffectKind::LocateCell); // Virtual +1A0/+1B8, then map lookup.
        const auto initial_index=index();
        if (require(cell().ramp,"overlay cell ramp is unresolved")>4 && initial_index!=178) {
            reject(); return out;
        }
        // These calls precede land, wall, and clearance checks.
        if (initial_index==24 || initial_index==25)
            effect(OverlayEffectKind::BridgeA,initial_index==24?0:6,1);
        else if (initial_index==237 || initial_index==238)
            effect(OverlayEffectKind::BridgeB,initial_index==237?0:6,1);
        else if (initial_index==167) {
            // Base 00710A60 / expansion 0074D670, then eight ordered writes.
            if (effect(OverlayEffectKind::VeinholeFits)) {
                for (int direction=0;direction<8;++direction) {
                    effect(OverlayEffectKind::WriteOverlay,178,0,0,direction);
                    effect(OverlayEffectKind::WriteData,0,0,0,direction);
                }
                effect(OverlayEffectKind::WriteOverlay,167);
                effect(OverlayEffectKind::WriteData,0);
                effect(OverlayEffectKind::CreateVeinhole);
            }
            finish(); return out;
        } else if (initial_index==126 && !overridden()) {
            // Base 0047AD00 / expansion 00485AF0.
            effect(OverlayEffectKind::CreateCrate);
            finish(); return out;
        }
        if (require(type().land,"overlay land field is unresolved")==9) {
            effect(OverlayEffectKind::WriteData,0);
            effect(OverlayEffectKind::WriteOverlay,index());
            finish(); return out;
        }
        if (require(type().wall,"overlay wall field is unresolved")) {
            // Base 004718B0 / expansion 0047C620, arguments (1,0,0).
            if (!effect(OverlayEffectKind::WallFits,1)) {
                effect(OverlayEffectKind::Dispose); reject(); return out;
            }
            effect(OverlayEffectKind::WriteData,0);
            effect(OverlayEffectKind::WriteOverlay,index());
            effect(OverlayEffectKind::UpdateWall,1);
            if (!overridden()) {
                effect(OverlayEffectKind::MarkZone);
                effect(OverlayEffectKind::RebuildZones);
            }
            if (require(env.import_token,"overlay import token is unresolved")!=-1)
                effect(OverlayEffectKind::WriteOwner50,
                    signed_word(require(env.frame,"overlay import frame is unresolved")));
            for (int direction=0;direction<8;++direction)
                effect(OverlayEffectKind::IncrementNeighbor,1,0,0,direction);
            finish(); return out;
        }
        const auto current_index=index();
        if ((current_index>=122 && current_index<=125) || (current_index>=233 && current_index<=236)) {
            out.status=OverlayMarkStatus::Unsupported;
            out.detail="original bridge approach geometry and RNG writes are not recovered";
            return out;
        }
        bool clear=true;
        if (!overridden()) {
            const auto slot=require(env.rules_slot,"overlay Rules identity is unresolved");
            if (slot==OverlayRulesSlot::FieldF8) clear=effect(OverlayEffectKind::Clearance,5,0,0);
            else if (slot==OverlayRulesSlot::FieldF4OrF0) clear=effect(OverlayEffectKind::Clearance,1,0,0);
            else if (!root_index(current_index)) {
                clear=effect(OverlayEffectKind::Clearance,1,1,1);
                if (current_index==102 && require(cell().ramp,"overlay cell ramp is unresolved")>4) clear=false;
            }
        }
        // Original rechecks the global after the clearance callback.
        bool blocked=false;
        if (!overridden()) {
            const auto existing=require(cell().overlay,"existing overlay index is unresolved");
            if (existing!=-1) {
                if (!cb.existing_blocks) throw Missing{"existing overlay type callback is missing"};
                blocked=require(cb.existing_blocks(existing),"existing overlay blocking field is unresolved");
            }
        }
        if (!blocked && clear) {
            effect(OverlayEffectKind::WriteOverlay,index());
            // This test retains the entry index, as does the original register.
            if (!root_index(initial_index)) effect(OverlayEffectKind::WriteData,0);
            if (require(type().land,"overlay land field is unresolved")==5) {
                effect(OverlayEffectKind::WriteData,1);
                effect(OverlayEffectKind::UpdateResource,0);
            }
            if (require(type().field_27a,"overlay field +27A/+2AA is unresolved"))
                effect(OverlayEffectKind::WriteData,255);
        }
        if (require(type().has_animation,"overlay animation field is unresolved"))
            effect(OverlayEffectKind::CreateAnimation);
        // A failed clearance check still executes setup and returns true.
        finish();
    } catch (const Missing& failure) {
        out.status=OverlayMarkStatus::NeedsInput; out.detail=failure.detail;
    }
    return out;
}
}
