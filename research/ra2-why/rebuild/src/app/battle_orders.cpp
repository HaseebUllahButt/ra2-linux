#include "battle_orders.h"
#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace ra2::app {
namespace {
using namespace session::world;
using sim::skirmish::Order;
using sim::skirmish::OrderKind;

// TODO-ORIGINAL: These pixel and time limits are native playability placeholders.
constexpr int hit_radius=16, drag_threshold=4, double_click_slop=4;
constexpr std::uint64_t double_click_ms=350;
// Input preserves SDL button numbers and key symbols. No SDL link is required here.
constexpr int left_button=1, right_button=3, escape_key=27;

bool contains(BattleRect rectangle, BattlePoint point) {
    const auto x=std::int64_t(point.x)-rectangle.x;
    const auto y=std::int64_t(point.y)-rectangle.y;
    return x>=0 && y>=0 && x<rectangle.width && y<rectangle.height;
}
BattlePoint clipped(BattlePoint point, BattleRect rectangle) {
    return {static_cast<int>(std::clamp<std::int64_t>(point.x, rectangle.x,
                std::int64_t(rectangle.x)+rectangle.width-1)),
            static_cast<int>(std::clamp<std::int64_t>(point.y, rectangle.y,
                std::int64_t(rectangle.y)+rectangle.height-1))};
}
const Entity* find(const Snapshot& world, std::uint32_t id) {
    const auto found=std::find_if(world.entities.begin(), world.entities.end(),
        [id](const auto& entity) { return entity.id==id; });
    return found==world.entities.end() ? nullptr : &*found;
}
bool live(const Entity& entity) {
    // Missing optional runtime fields do not imply death or absence.
    return entity.id && entity.lifetime==Lifetime::Placed && entity.on_map!=false &&
        entity.active!=false && entity.in_limbo!=true &&
        (!entity.resolved_hp || *entity.resolved_hp>0);
}
bool mcv(const Entity& entity) {
    if (entity.kind!=Kind::Unit) return false;
    auto matches=[&](std::string_view name) {
        if (entity.type.size()!=name.size()) return false;
        for (std::size_t i=0; i<name.size(); ++i) {
            const auto c=entity.type[i];
            if ((c>='a' && c<='z' ? c-'a'+'A' : c)!=name[i]) return false;
        }
        return true;
    };
    // TODO-ORIGINAL: Resolve deploy capability from original type rules, including mods.
    return matches("AMCV") || matches("SMCV") || matches("PCV");
}
bool enemy_of(const Entity& entity, const Snapshot& world, std::uint32_t house) {
    if (!house || !entity.house_id || entity.house_id==house || entity.kind==Kind::Terrain)
        return false;
    const auto found=std::find_if(world.houses.begin(), world.houses.end(),
        [&](const auto& other) { return other.id==entity.house_id; });
    if (found!=world.houses.end() &&
        (found->role==HouseRole::Neutral || found->role==HouseRole::Special)) return false;
    // TODO-ORIGINAL: Snapshot has no alliance or disguise state. Other player houses are hostile.
    return true;
}
} // namespace

BattleOrders::BattleOrders(std::uint32_t house) : house_(house) {}

bool BattleOrders::selectable(const session::world::Entity& entity) const {
    // TODO-ORIGINAL: Recover type selectability, cargo, cloak, and discovery checks.
    // Root's projection callback must reject hidden objects until those fields exist.
    return house_ && entity.house_id==house_ && live(entity) &&
        (entity.kind==Kind::Unit || entity.kind==Kind::Infantry);
}
void BattleOrders::cancel_pointer() {
    press_.reset();
    rectangle_.reset();
    dragging_=false;
}
void BattleOrders::clear() {
    selected_.clear();
    cancel_input();
}
void BattleOrders::cancel_input() {
    cancel_pointer();
    last_click_.reset();
}
void BattleOrders::set_placement_active(bool active) {
    if (placement_==active) return;
    placement_=active;
    cancel_pointer();
    last_click_.reset();
}
void BattleOrders::sync(const session::world::Snapshot& world) {
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(), [&](auto id) {
        const auto* entity=find(world, id);
        return !entity || !selectable(*entity);
    }), selected_.end());
    if (last_click_) {
        const auto* entity=find(world, last_click_->id);
        if (!entity || !selectable(*entity)) last_click_.reset();
    }
}
void BattleOrders::update_drag(BattlePoint point, BattleRect battlefield) {
    if (!press_) return;
    if (!contains(battlefield, *press_)) {
        cancel_pointer();
        last_click_.reset();
        return;
    }
    if (std::abs(std::int64_t(point.x)-press_->x)>=drag_threshold ||
        std::abs(std::int64_t(point.y)-press_->y)>=drag_threshold) dragging_=true;
    if (!dragging_) return;
    point=clipped(point, battlefield);
    rectangle_=BattleRect{std::min(press_->x,point.x), std::min(press_->y,point.y),
        static_cast<int>(std::abs(std::int64_t(point.x)-press_->x)+1),
        static_cast<int>(std::abs(std::int64_t(point.y)-press_->y)+1)};
}
const session::world::Entity* BattleOrders::pick(const session::world::Snapshot& world,
    const BattleView& view, BattlePoint point, bool enemy) const {
    if (!view.project) return nullptr;
    const Entity* result=nullptr;
    std::int64_t best=hit_radius*hit_radius+1;
    for (const auto& entity : world.entities) {
        if (enemy ? (!live(entity) || !enemy_of(entity,world,house_)) : !selectable(entity))
            continue;
        if (visible_ && !visible_(entity)) continue;
        const auto anchor=view.project(entity);
        if (!anchor || !contains(view.battlefield,*anchor)) continue;
        const auto dx=std::int64_t(anchor->x)-point.x, dy=std::int64_t(anchor->y)-point.y;
        if (std::abs(dx)>hit_radius || std::abs(dy)>hit_radius) continue;
        const auto distance=dx*dx+dy*dy;
        // TODO-ORIGINAL: Prefer the nearest anchor, then the lower ID for equal distances.
        if (distance<=hit_radius*hit_radius &&
            (distance<best || (distance==best && result && entity.id<result->id))) {
            result=&entity;
            best=distance;
        }
    }
    return result;
}
std::vector<sim::skirmish::Order> BattleOrders::orders(const session::world::Snapshot& world,
    sim::skirmish::OrderKind kind, std::uint32_t target, ra2map::Cell cell) const {
    std::vector<Order> result;
    for (const auto id : selected_) {
        const auto* entity=find(world,id);
        if (!entity || !selectable(*entity) || (kind==OrderKind::Deploy && !mcv(*entity))) continue;
        Order order;
        order.kind=kind;
        order.house=house_;
        order.entity=id;
        order.target=target;
        order.cell=cell;
        result.push_back(std::move(order));
    }
    return result;
}
std::vector<sim::skirmish::Order> BattleOrders::handle(const Input& input,
    const session::world::Snapshot& world, const BattleView& view, std::uint64_t now_ms) {
    sync(world);
    if (input.kind==InputKind::Focus) {
        focused_=input.pressed;
        cancel_pointer();
        last_click_.reset();
        return {};
    }
    if (input.kind==InputKind::Quit || input.kind==InputKind::Resize) {
        cancel_pointer();
        last_click_.reset();
        return {};
    }
    if (!focused_) return {};
    if (input.kind==InputKind::Key) {
        if (!input.pressed) return {};
        if (input.key==escape_key) {
            cancel_pointer();
            last_click_.reset();
            if (placement_) placement_=false;
            else selected_.clear();
            return {};
        }
        if (placement_) return {};
        if (input.key=='d' || input.key=='D' || input.key=='s' || input.key=='S') {
            cancel_pointer();
            last_click_.reset();
            return orders(world,(input.key=='d' || input.key=='D') ? OrderKind::Deploy : OrderKind::Stop);
        }
        return {};
    }
    if (placement_) return {};
    const BattlePoint point{input.x,input.y};
    if (input.kind==InputKind::Motion) {
        update_drag(point,view.battlefield);
        return {};
    }
    if (input.kind!=InputKind::Button) return {};
    if (input.key==right_button) {
        if (!input.pressed) return {};
        if (!contains(view.battlefield,point)) return {};
        cancel_pointer();
        last_click_.reset();
        selected_.clear();
        return {};
    }
    if (input.key!=left_button) return {};
    if (input.pressed) {
        if (press_) return {};
        if (!contains(view.battlefield,point)) {
            last_click_.reset();
            return {};
        }
        press_=point;
        return {};
    }
    if (!press_) return {};
    update_drag(point,view.battlefield);
    if (!press_) return {};
    const auto rectangle=rectangle_;
    cancel_pointer();
    if (rectangle) {
        selected_.clear();
        last_click_.reset();
        if (view.project) for (const auto& entity : world.entities) {
            if (!selectable(entity) || (visible_ && !visible_(entity))) continue;
            const auto anchor=view.project(entity);
            if (anchor && contains(view.battlefield,*anchor) && contains(*rectangle,*anchor))
                selected_.push_back(entity.id);
        }
        std::sort(selected_.begin(),selected_.end());
        selected_.erase(std::unique(selected_.begin(),selected_.end()),selected_.end());
        return {};
    }
    if (!contains(view.battlefield,point)) {
        last_click_.reset();
        return {};
    }
    const auto* entity=pick(world,view,point,false);
    if (!entity) {
        last_click_.reset();
        if (!selected_.empty()) {
            if (const auto* target=pick(world,view,point,true))
                return orders(world,OrderKind::Attack,target->id);
            if (view.screen_to_cell) if (const auto cell=view.screen_to_cell(point))
                return orders(world,OrderKind::Move,0,*cell);
        }
        return {};
    }
    selected_.clear();
    selected_.push_back(entity->id);
    if (mcv(*entity) && last_click_ && last_click_->id==entity->id &&
        now_ms>=last_click_->time_ms && now_ms-last_click_->time_ms<=double_click_ms &&
        std::abs(std::int64_t(point.x)-last_click_->point.x)<=double_click_slop &&
        std::abs(std::int64_t(point.y)-last_click_->point.y)<=double_click_slop) {
        last_click_.reset();
        return orders(world,OrderKind::Deploy);
    }
    last_click_=Click{entity->id,point,now_ms};
    return {};
}
} // namespace ra2::app
