#pragma once
#include "overlay_mark.h"
#include <array>
#include <stdexcept>
namespace ra2::sim::test {
struct OverlayFixture {
    struct Cell { std::int32_t overlay=-1; std::uint8_t data=0,count=0; std::uint32_t owner=0; };
    std::array<Cell,9> cells;
    OverlayMarkType type;
    OverlayMarkEnvironment env;
    OverlayMarkCallbacks callbacks;
    unsigned mode=0,ramp=0;
    bool parent=false,clear=false,wall_fits=false,vein_fits=false,blocks=false,mutate=false;
    unsigned actor74=1,limbo=0;
    explicit OverlayFixture(unsigned seed) {
        std::uint32_t value=seed^0x93c56a71;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        static constexpr int indexes[]={24,25,237,238,167,126,178,102,0,1,121,127,232,239};
        type.index=indexes[next()%14]; mode=next()%5; ramp=next()%8;
        type.land=std::array<int,3>{0,5,9}[next()%3]; type.wall=next()%4==0;
        env.placement_override=next()%3==0; env.rules_slot=static_cast<OverlayRulesSlot>(next()%3);
        env.import_token=next()%2?-1:17; env.frame=0x87654321u;
        const auto existing=next()%2?-1:40;
        parent=next()%4!=0; clear=next()%2; wall_fits=next()%2; vein_fits=next()%2;
        blocks=next()%2; type.field_27a=next()%2!=0; type.has_animation=seed%7==0; mutate=next()%2;
        // Exercise all type branches before random early exits dominate the grid.
        if (seed<256) { mode=seed%2?1:3; ramp=seed%5; parent=true; }
        for (unsigned i=0;i<cells.size();++i) {
            cells[i].overlay=i==0?existing:-1;
            cells[i].data=static_cast<std::uint8_t>(seed+i*29);
            cells[i].count=static_cast<std::uint8_t>(seed+i*37);
            cells[i].owner=0x12340000u+i;
        }
        callbacks.type=[&]() ->std::optional<OverlayMarkType> { return type; };
        callbacks.cell=[&]() ->std::optional<OverlayMarkCell> { return OverlayMarkCell{static_cast<std::uint8_t>(ramp),cells[0].overlay}; };
        callbacks.existing_blocks=[&](std::int32_t) ->std::optional<bool> { return blocks; };
        callbacks.effect=[&](const OverlayEffect& effect) ->std::optional<bool> {
            auto& cell=cells.at(effect.neighbor<0?0:static_cast<unsigned>(effect.neighbor+1));
            switch (effect.kind) {
            case OverlayEffectKind::ParentMark: return parent;
            case OverlayEffectKind::VeinholeFits: return vein_fits;
            case OverlayEffectKind::WallFits: return wall_fits;
            case OverlayEffectKind::Clearance:
                if (mutate) env.placement_override=true;
                return clear;
            case OverlayEffectKind::WriteOverlay: cell.overlay=effect.value; break;
            case OverlayEffectKind::WriteData: cell.data=static_cast<std::uint8_t>(effect.value); break;
            case OverlayEffectKind::WriteOwner50: cell.owner=static_cast<std::uint32_t>(effect.value); break;
            case OverlayEffectKind::IncrementNeighbor: ++cell.count; break;
            case OverlayEffectKind::FinishObject: actor74=0; limbo=1; break;
            case OverlayEffectKind::UpdateResource:
                if (mutate) type.field_27a=!*type.field_27a;
                break;
            default: break;
            }
            return false; // Ignored calls must not interpret this as rejection.
        };
    }
    std::uint32_t digest(const OverlayMarkResult& result) const {
        std::uint32_t h=2166136261u;
        const auto mix=[&](std::uint32_t v) { h=(h^v)*16777619u; };
        mix(static_cast<unsigned>(result.status));
        for (const auto& effect:result.effects) {
            mix(static_cast<unsigned>(effect.kind)); mix(effect.value); mix(effect.second);
            mix(effect.third); mix(effect.neighbor);
        }
        for (const auto& cell:cells) { mix(cell.overlay); mix(cell.data); mix(cell.owner); mix(cell.count); }
        mix(actor74); mix(limbo); return h;
    }
};
}
