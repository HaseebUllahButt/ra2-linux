#pragma once

#include "platform.h"
#include "../session/world/world.h"
#include "../sim/skirmish_order.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace ra2::app {
struct BattlePoint { int x=0, y=0; };
// Screen pixels. The right and bottom edges are excluded.
struct BattleRect { int x=0, y=0, width=0, height=0; };
struct BattleView {
    BattleRect battlefield;
    // Return the visible selection anchor in Input's logical screen pixels.
    // Return no point for hidden objects or unresolved positions.
    std::function<std::optional<BattlePoint>(const session::world::Entity&)> project;
    // Return no Cell for an invalid destination. Root owns map bounds and camera math.
    std::function<std::optional<ra2map::Cell>(BattlePoint)> screen_to_cell;
};

// Native skirmish input only. This class never executes orders or changes the world.
// TODO-ORIGINAL: Recover hit shapes, drag and double-click limits, overlap priority,
// selection modifiers, groups, and original deploy input rules.
// Left clicks select or order. Right clicks cancel selection.
// Boxes include owned Unit and Infantry anchors.
class BattleOrders {
public:
    explicit BattleOrders(std::uint32_t house);
    // Feed each event once, in poll order. now_ms is a monotonic millisecond clock.
    // Left selection and orders commit on release. Right press cancels.
    std::vector<sim::skirmish::Order> handle(const Input&,
        const session::world::Snapshot&, const BattleView&, std::uint64_t now_ms);
    // Call once per frame too, including frames without input. Drop stale selected IDs.
    void sync(const session::world::Snapshot&);
    const std::vector<std::uint32_t>& selected_ids() const noexcept { return selected_; }
    // A clipped drag box, in screen pixels. No box means no active drag.
    std::optional<BattleRect> selection_rectangle() const noexcept { return rectangle_; }
    // Clear selection and pointer history.
    void clear();
    // Close a modal panel without committing its pointer release as an order.
    // Keep the selected units.
    void cancel_input();
    // Root owns placement and its orders. While active, normal battle input is blocked.
    // Escape cancels this flag first and keeps selection. A later Escape clears selection.
    void set_placement_active(bool active);
    bool placement_active() const noexcept { return placement_; }
    // Optional shroud filter. Objects it rejects cannot be clicked, box-selected
    // or targeted; a click on a hidden enemy orders a move to that ground.
    // Pass BattleShroud::visible_filter(house), or nothing to clear it.
    using VisibleFilter = std::function<bool(const session::world::Entity&)>;
    void set_visible_filter(VisibleFilter filter) { visible_ = std::move(filter); }
private:
    struct Click {
        std::uint32_t id;
        BattlePoint point;
        std::uint64_t time_ms;
    };
    void cancel_pointer();
    void update_drag(BattlePoint, BattleRect);
    bool selectable(const session::world::Entity&) const;
    const session::world::Entity* pick(const session::world::Snapshot&,
        const BattleView&, BattlePoint, bool enemy) const;
    std::vector<sim::skirmish::Order> orders(const session::world::Snapshot&,
        sim::skirmish::OrderKind, std::uint32_t target=0, ra2map::Cell cell={}) const;

    std::uint32_t house_;
    VisibleFilter visible_;
    std::vector<std::uint32_t> selected_;
    std::optional<BattlePoint> press_;
    std::optional<BattleRect> rectangle_;
    std::optional<Click> last_click_;
    bool dragging_=false, placement_=false, focused_=true;
};
} // namespace ra2::app
