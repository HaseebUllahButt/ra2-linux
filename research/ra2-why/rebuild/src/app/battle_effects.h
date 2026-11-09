#pragma once
#include "../sim/damage_runtime.h"
#include "battle_cursor.h"
#include <functional>
#include <map>

namespace ra2::app {
struct EffectPoint { int x=0,y=0; };
// Presentation observer. All positions are map pixels. No world or RNG writes.
class BattleEffects {
public:
    using Project = std::function<std::optional<EffectPoint>(sim::Coord)>;
    explicit BattleEffects(const fs::MixFileSystem&);
    // Call once at setup and after each simulation tick. Never use wall time.
    void observe(const session::world::Snapshot&, const std::vector<sim::PrototypeCombatProjectile>&,
                 std::uint64_t frame, const Project&);
    void draw(render::Bitmap&, int camera_x, int camera_y, std::uint64_t frame) const;
    std::size_t active() const { return effects_.size(); }
private:
    enum Kind { Flash, Hit, Death, Tracer };
    struct Effect { Kind kind; EffectPoint at,to; std::uint64_t born; };
    struct Seen { int hp; sim::Coord position; std::optional<std::int32_t> weapon_timer; };
    std::map<std::uint32_t,Seen> seen_;
    std::vector<Effect> effects_;
    std::vector<EffectPoint> shells_;
    std::vector<gfx::RgbaImage> flash_,hit_,death_;
    bool ready_=false;
    std::uint64_t frame_=0;
    std::uint64_t lifetime(Kind) const;
};
}
