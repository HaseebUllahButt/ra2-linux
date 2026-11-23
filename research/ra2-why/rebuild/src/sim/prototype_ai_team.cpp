// Teams of the computer opponent: the AI trigger picker (006BED50), the
// trigger test (0041C990), TeamClass (006B7430 ... 006BE440) and the
// script actions that base AI.INI uses.
#include "prototype_ai_internal.h"

namespace ra2::sim {

const PrototypeAI::TaskForce* PrototypeAI::task_force(const TeamType& type) const {
    const auto found = std::find_if(task_forces_.begin(), task_forces_.end(),
        [&](const TaskForce& f) { return f.id == type.task_force; });
    return found == task_forces_.end() ? nullptr : &*found;
}

std::size_t PrototypeAI::team_type_index(const std::string& id) const {
    for (std::size_t i = 0; i < team_types_.size(); ++i) if (team_types_[i].id == id) return i;
    return team_types_.size();
}

// Base 0041C990 condition part. Counts are owned objects of the type
// (00491D10); the comparator is the first {value, operator} pair.
bool PrototypeAI::condition(const Context& context, const Trigger& trigger) const {
    const auto& object = trigger.condition_object;
    // Without a selected enemy only -1 (always) and 1 (own house owns)
    // are tested; every other condition fails.
    if (!context.enemy_available && trigger.condition != -1 && trigger.condition != 1) return false;
    switch (trigger.condition) {
    case -1: return true;
    case 0: return !object.empty() && compare(context.count(context.player.id, object), trigger.number, trigger.comparator);
    case 1: return !object.empty() && compare(context.count(context.computer.id, object), trigger.number, trigger.comparator);
    case 2: case 3: {
        // Yellow power is a reserve below 100; red is below zero.
        std::int64_t output = 0, drain = 0;
        for (const auto& entity : context.world.entities) {
            if (entity.house_id != context.player.id || entity.kind != WorldKind::Structure || !live(entity)) continue;
            const auto power = rules_.read_integer(entity.type, "Power");
            if (power > 0) output += power;
            else drain -= static_cast<std::int64_t>(power);
        }
        output = context.player.power_output.value_or(static_cast<std::int32_t>(output));
        drain = context.player.power_drain.value_or(static_cast<std::int32_t>(drain));
        return output - drain < (trigger.condition == 2 ? 100 : 0);
    }
    case 4: return compare(context.player.cash.value_or(0), trigger.number, trigger.comparator); // 0041D490
    case 7: { // 0041CEF0
        if (object.empty()) return false;
        std::int64_t total = 0;
        // TODO-ORIGINAL: Civilian houses are the [Sides] entry that holds Neutral.
        for (const auto& house : context.world.houses)
            if (equal_name(house.country, "Neutral") || equal_name(house.country, "Special"))
                total += context.count(house.id, object);
        return compare(total, trigger.number, trigger.comparator);
    }
    // TODO-ORIGINAL: 5 and 6 (0041D330, 0041D3E0) test the enemy's Iron
    // Curtain and Chronosphere charge; the match has no super weapon timers.
    default: return false;
    }
}

// Base 004F3630: each TaskForce type has a factory (FindFactory with
// requireCanBuild false). In a skirmish (00A3D298) a missing factory
// rejects the team, even if idle units of the type exist.
// TODO-ORIGINAL: the requirePower factory test is not modelled.
bool PrototypeAI::factories_for(const Context& context, std::size_t type) const {
    const auto* force = task_force(team_types_[type]);
    if (!force) return false;
    for (const auto& [count, name] : force->members) {
        (void)count;
        const auto* kind = factory_kind(rules_, name);
        if (!kind || !factory(rules_, context.buildings, kind, rules_.read_bool(name, "Naval"))) return false;
    }
    return true;
}

// Base 004F35F0: teams of the type that the house has now.
std::int32_t PrototypeAI::team_count(std::size_t type) const {
    return static_cast<std::int32_t>(std::count_if(teams_.begin(), teams_.end(),
        [&](const Team& team) { return team.type == type; }));
}

// Base 0041C990 (AITriggerTypeClass::Is_Ready) in its own order.
bool PrototypeAI::trigger_ready(const Context& context, std::size_t index, bool enough_defense) const {
    const auto& trigger = triggers_[index];
    const auto first = team_type_index(trigger.team1);
    if (first == team_types_.size()) return false;
    const auto second = trigger.team2.empty() ? team_types_.size() : team_type_index(trigger.team2);
    // Both teams (+DC, +E0) must be IsBaseDefense (+EE) for a defense pick.
    const bool defense = team_types_[first].base_defense &&
        (second == team_types_.size() || team_types_[second].base_defense);
    // No enemy (+54F0), or UseMinDefenseRule (Rules +1437) with fewer live
    // defense teams (House +555C) than MinimumAIDefensiveTeams (Rules
    // +FFC): only defense picks, and only below the defense maximum.
    const bool need_defense = !context.enemy_available || (rules_.read_bool("General", "UseMinDefenseRule") &&
        defense_teams() < by_difficulty(rules_, "MinimumAIDefensiveTeams", difficulty_, 0));
    if (need_defense && !defense) return false;
    if (defense && enough_defense) return false;
    // In a skirmish the trigger needs IsForSkirmish (+D0) and the flag of
    // the house's AIDifficulty (+C8): 0 Hard (+D4), 1 Normal (+D3), 2 Easy (+D2).
    const bool enabled = difficulty_ == Difficulty::Easy ? trigger.easy :
        difficulty_ == Difficulty::Normal ? trigger.normal : trigger.hard;
    if (!trigger.skirmish || !enabled) return false;
    // +A0: 0 no house (never), 1 one house (+A8), 2 all houses.
    if (none(trigger.house)) return false;
    if (!equal_name(trigger.house, "<all>") && !equal_name(trigger.house, context.computer.country)) return false;
    // +AC: 1 needs side 0 (House +130), 2 needs side 1; others pass.
    if (trigger.side == 1 && context.side.value_or(-1) != 0) return false;
    if (trigger.side == 2 && context.side.value_or(-1) != 1) return false;
    if (context.computer.tech_level.value_or(10) < trigger.tech_level) return false;
    if (!condition(context, trigger)) return false;
    // TODO-ORIGINAL: 0041E130 also needs both base centres in one movement
    // zone for TeamTypes that ask for it (+E8); zones are not modelled here.
    if (!factories_for(context, first) || (second != team_types_.size() && !factories_for(context, second)))
        return false;
    for (const auto type : {first, second}) {
        if (type == team_types_.size()) continue;
        const auto max = team_types_[type].max;
        if (max >= 0 && max <= team_count(type)) return false;
    }
    return true;
}

std::int32_t PrototypeAI::defense_teams() const {
    return static_cast<std::int32_t>(std::count_if(teams_.begin(), teams_.end(),
        [&](const Team& team) { return team_types_[team.type].base_defense; }));
}

// Base 006BED50, called by HouseAI 004E4110 when the team timer expires.
// Returns the TeamTypes to create; each is marked Autocreate (+A9).
std::vector<std::pair<std::size_t, std::size_t>> PrototypeAI::pick_teams(const Context& context) {
    std::vector<std::pair<std::size_t, std::size_t>> list; // {type, trigger}
    // +554C RatioAITriggerTeam (100 unless a map sets it) and +13A, which
    // 004F4070 sets when the house starts its base.
    const auto roll = random(1, 100);
    if (static_cast<std::int32_t>(roll) <= ratio_ai_trigger_team_ && ai_started_) {
        const auto cap = by_difficulty(rules_, "TotalAITeamCap", difficulty_, 30);
        const auto max_defense = by_difficulty(rules_, "MaximumAIDefensiveTeams", difficulty_, 2);
        auto total = static_cast<std::int32_t>(teams_.size());
        const auto defensive = defense_teams();
        bool enough_defense = false;
        if (total < cap || defensive < total / 2) {
            if (max_defense < defensive) enough_defense = true;
        } else {
            // At the cap with half the teams on defense: delete the oldest
            // (least CreationFrame +50) defense team.
            auto release = teams_.end();
            for (auto it = teams_.begin(); it != teams_.end(); ++it)
                if (team_types_[it->type].base_defense && (release == teams_.end() || it->created < release->created))
                    release = it;
            if (release != teams_.end()) {
                --total;
                enough_defense = true;
                finish(*release);
                teams_.erase(release);
            }
        }
        if (total < cap) {
            std::vector<std::pair<std::size_t, std::int64_t>> choices;
            std::int64_t sum = 0;
            bool exclusive = false;
            for (std::size_t i = 0; i < triggers_.size(); ++i) {
                if (!trigger_ready(context, i, enough_defense)) continue;
                const auto weight = static_cast<std::int64_t>(weights_[i]); // 0077F110 truncates
                // Weight 5000 is infinite: the first one clears the list
                // and only other 5000 weights join it.
                if (weight == 5000) {
                    if (!exclusive) { exclusive = true; choices.clear(); sum = 0; }
                } else if (exclusive) continue;
                choices.emplace_back(i, weight);
                sum += weight;
            }
            if (sum != 0 && !choices.empty()) {
                const auto pick = static_cast<std::int64_t>(random(1, static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(sum, 1, 0x7fffffff))));
                std::int64_t walk = 0;
                for (const auto& [index, weight] : choices) {
                    walk += weight;
                    if (pick > walk) continue;
                    for (const auto& id : {triggers_[index].team1, triggers_[index].team2}) {
                        const auto type = id.empty() ? team_types_.size() : team_type_index(id);
                        if (type < team_types_.size()) list.emplace_back(type, index);
                    }
                    break;
                }
            }
        }
    }
    // A team of a picked type that is reforming (+7B) or not yet moving
    // (+7F) cancels the whole pick.
    for (const auto& team : teams_)
        if (team.reforming || !team.moving)
            for (const auto& [type, trigger] : list) {
                (void)trigger;
                if (team.type == type) return {};
            }
    for (const auto& [type, trigger] : list) {
        (void)trigger;
        team_types_[type].autocreate = true;
    }
    return list;
}

// Base 006B7430 (TeamClass constructor): CreationFrame is the current
// frame; IsUnderStrength, IsAltered and IsNextMission start set; the
// script starts before its first line. A base defense team counts in
// House +555C.
void PrototypeAI::make_team(const Context& context, std::size_t type, std::size_t trigger) {
    Team team;
    team.type = type;
    team.trigger = trigger;
    team.created = context.frame;
    teams_.push_back(std::move(team));
}

// Base 006B7770 (TeamClass destructor): every AI trigger whose first team
// is this TeamType gets AITriggerSuccessWeightDelta when script action 49
// set +83, else AITriggerFailureWeightDelta, clamped to its minimum and
// maximum weight. The members leave the team.
void PrototypeAI::finish(Team& team) {
    const bool success = team.succeeded;
    const auto delta = rules_.read_double("General",
        success ? "AITriggerSuccessWeightDelta" : "AITriggerFailureWeightDelta", success ? 20.0 : -50.0);
    for (std::size_t i = 0; i < triggers_.size(); ++i) {
        if (triggers_[i].team1 != team_types_[team.type].id) continue;
        weights_[i] = std::max(triggers_[i].minimum, std::min(triggers_[i].maximum, weights_[i] + delta));
    }
    for (const auto id : team.members) members_.erase(id);
    team.members.clear();
    team.dead = true;
}


namespace {
constexpr std::int32_t mission_attack = 1, mission_move = 2, mission_guard = 5, mission_area_guard = 11,
    mission_unload = 16;
constexpr std::int32_t frames_per_second_team = 15; // 006B7AD0 case 5: argument * 15

struct Lepton { double x = 0, y = 0, z = 0; };

Lepton coord_of(const WorldEntity& entity) {
    if (entity.position) return {double(entity.position->x), double(entity.position->y), double(entity.position->z)};
    return {entity.cell->x * 256.0 + 128, entity.cell->y * 256.0 + 128, 0};
}
Lepton coord_of(ra2map::Cell cell) { return {cell.x * 256.0 + 128, cell.y * 256.0 + 128, 0}; }

// 00412F00 style 3D distance in leptons, truncated (0077F110).
std::int64_t lepton_distance(Lepton a, Lepton b) {
    const auto dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return static_cast<std::int64_t>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

ra2map::Cell cell_of(Lepton c) {
    const auto clamp = [](double v) { return static_cast<std::uint16_t>(std::clamp(std::floor(v / 256), 0.0, 65535.0)); };
    return {clamp(c.x), clamp(c.y)};
}

bool same_cell(const std::optional<ra2map::Cell>& a, const std::optional<ra2map::Cell>& b) {
    return a.has_value() == b.has_value() && (!a || (a->x == b->x && a->y == b->y));
}
} // namespace

// Rules Stray (+1384) and RelaxedStray (+1388), in leptons. Script
// actions 53 and 54 use the relaxed value (006BA1A0, 006B9F40, 006BA600).
static std::int64_t stray_for(const IniFile& rules, std::int32_t action) {
    const bool relaxed = action == 53 || action == 54;
    return static_cast<std::int64_t>(rules.read_double("General", relaxed ? "RelaxedStray" : "Stray",
        relaxed ? 3.0 : 2.0) * 256.0);
}

// Base Assign_Mission (+1D0), Assign_Target (+370) and Assign_Destination
// (+410) for a member. Orders are only sent for a change, as the
// original only writes a new value; a blocked move is retried after 15
// frames instead of every frame.
void PrototypeAI::assign(Context& context, std::uint32_t id, std::int32_t mission,
                         std::optional<ra2map::Cell> destination, std::uint32_t target,
                         std::vector<skirmish::Order>& result) {
    const auto found = context.own.find(id);
    if (found == context.own.end()) return;
    auto& member = members_[id];
    const auto& entity = *found->second;
    if (mission == mission_move && destination) {
        if (member.mission == mission_move && same_cell(member.destination, destination) &&
            (entity.locomotor_moving.value_or(false) || context.frame - member.order_frame < 15)) return;
        auto item = order(skirmish::OrderKind::Move, context.computer.id, entity);
        item.cell = *destination;
        result.push_back(std::move(item));
        member.order_frame = context.frame;
    } else if (mission == mission_attack && target) {
        const auto victim = context.targets.find(target);
        if (victim == context.targets.end()) return;
        if (member.mission == mission_attack && member.target == target && context.frame - member.order_frame < 15) return;
        auto item = order(skirmish::OrderKind::Attack, context.computer.id, entity);
        item.target = target;
        item.cell = *victim->second->cell;
        result.push_back(std::move(item));
        member.order_frame = context.frame;
    } else if (member.mission != mission || member.destination || member.target) {
        result.push_back(order(skirmish::OrderKind::Stop, context.computer.id, entity));
        member.order_frame = context.frame;
    }
    member.mission = mission;
    member.destination = destination;
    member.target = target;
}

// A member still has a NavCom while it moves to its ordered destination.
bool PrototypeAI::has_navcom(const Context& context, std::uint32_t id) {
    auto& member = members_[id];
    if (!member.destination) return false;
    const auto found = context.own.find(id);
    if (found == context.own.end()) return false;
    if (found->second->locomotor_moving.value_or(false) || context.frame <= member.order_frame + 1) return true;
    member.destination.reset();
    return false;
}

// A member still has a TarCom while its target lives and it fights it.
bool PrototypeAI::has_tarcom(const Context& context, std::uint32_t id) const {
    const auto found = context.own.find(id);
    if (found == context.own.end()) return false;
    const auto target = found->second->target_id.value_or(0);
    return target != 0 && context.targets.count(target);
}

std::optional<ra2map::Cell> PrototypeAI::focus_cell(const Context& context, const Focus& focus) const {
    if (focus.cell) return focus.cell;
    if (const auto found = context.targets.find(focus.object); found != context.targets.end()) return *found->second->cell;
    if (const auto found = context.own.find(focus.object); found != context.own.end()) return *found->second->cell;
    return std::nullopt;
}

// Base 006B79E0 (AssignMissionTarget): members that go to or attack the
// old QueuedFocus are set to Guard; Focus follows when it was empty or
// equal to the old QueuedFocus.
void PrototypeAI::assign_mission_target(Context& context, Team& team, Focus target,
                                        std::vector<skirmish::Order>& result) {
    const auto old = team.queued;
    if (!(target.object == old.object && same_cell(target.cell, old.cell)) && !old.empty()) {
        const auto old_cell = focus_cell(context, old);
        for (const auto id : team.members) {
            const auto& member = members_[id];
            const bool at_target = old.object && member.target == old.object;
            const bool at_destination = old_cell && same_cell(member.destination, old_cell);
            if (at_target || at_destination)
                assign(context, id, mission_guard, at_destination ? std::nullopt : member.destination,
                       at_target ? 0 : member.target, result);
        }
    }
    if ((team.focus.object == old.object && same_cell(team.focus.cell, old.cell)) || team.focus.empty())
        team.focus = target;
    team.queued = target;
}

// Base 006B9610 (Calc_Center): the Zone is the Cell at the mean position
// of the members, and ClosestMember the member nearest to it.
// TODO-ORIGINAL: GuardSlower members count twice, and a Zone Cell that
// the closest member cannot enter becomes that member.
void PrototypeAI::calc_center(const Context& context, Team& team) const {
    team.zone.reset();
    team.closest = 0;
    double x = 0, y = 0;
    std::size_t count = 0;
    for (const auto id : team.members) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        const auto c = coord_of(*found->second);
        x += c.x;
        y += c.y;
        ++count;
    }
    if (!count) return;
    const Lepton centre{static_cast<double>(static_cast<std::int64_t>(x) / static_cast<std::int64_t>(count)),
                        static_cast<double>(static_cast<std::int64_t>(y) / static_cast<std::int64_t>(count)), 0};
    team.zone = cell_of(centre);
    std::int64_t best = -1;
    for (const auto id : team.members) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        const auto d = lepton_distance(coord_of(*found->second), centre);
        if (best == -1 || d < best) { best = d; team.closest = id; }
    }
}

// Base 006B8DC0 (Can_Add): the object is alive, of the team's house, in no
// team or in a team of lower Priority, in a recruitable mission, and its
// type is a TaskForce entry with room. An Autocreate team (+A9) needs the
// object's Recruitable flag (Techno +?, set from AreTeamMembersRecruitable
// of its last team).
bool PrototypeAI::can_add(const Context& context, const Team& team, const WorldEntity& entity,
                          std::size_t& entry) const {
    if (entity.house_id != context.computer.id || entity.kind == WorldKind::Structure || !live(entity)) return false;
    if (rules_.read_bool(entity.type, "Harvester")) return false; // Harvest is not recruitable
    const auto owner = team_of(entity.id);
    if (owner == &team) return false;
    const auto& type = team_types_[team.type];
    const auto member = members_.find(entity.id);
    const bool recruitable = member == members_.end() || member->second.recruitable;
    if (type.autocreate && !recruitable) return false;
    if (owner && team_types_[owner->type].priority >= type.priority) return false;
    const auto* force = task_force(type);
    if (!force) return false;
    for (std::size_t i = 0; i < force->members.size(); ++i)
        if (equal_name(force->members[i].second, entity.type)) {
            entry = i;
            return team.quantity[i] < force->members[i].first;
        }
    return false;
}

const PrototypeAI::Team* PrototypeAI::team_of(std::uint32_t id) const {
    for (const auto& team : teams_)
        if (!team.dead && std::find(team.members.begin(), team.members.end(), id) != team.members.end()) return &team;
    return nullptr;
}

// Base 006B8CC0 (Add): the new member goes to the head of the list; the
// first member is the team leader (+165). A member of another team leaves
// it first (006B8FB0).
void PrototypeAI::add_member(Context& context, Team& team, std::uint32_t id, std::size_t entry) {
    for (auto& other : teams_)
        if (&other != &team && !other.dead &&
            std::find(other.members.begin(), other.members.end(), id) != other.members.end())
            remove_member(context, other, id);
    auto& member = members_[id];
    member.leader = team.members.empty();
    member.initiated = false;
    if (const auto found = context.own.find(id); found != context.own.end()) member.type = found->second->type;
    team.members.insert(team.members.begin(), id);
    ++team.quantity[entry];
    if (!team.zone) calc_center(context, team);
    team.altered = true;
    member.recruitable = team_types_[team.type].recruitable;
}

// Base 006B8FB0 (Remove): the count of the member's entry drops, and when
// no leader is left the first member is initiated and the Zone is reset.
void PrototypeAI::remove_member(Context& context, Team& team, std::uint32_t id) {
    const auto at = std::find(team.members.begin(), team.members.end(), id);
    if (at == team.members.end()) return;
    team.members.erase(at);
    if (const auto* force = task_force(team_types_[team.type])) {
        const auto found = context.own.find(id);
        const auto type = found == context.own.end() ? members_[id].type : found->second->type;
        for (std::size_t i = 0; i < force->members.size(); ++i)
            if (equal_name(force->members[i].second, type)) { --team.quantity[i]; break; }
    }
    const bool leader_left = std::any_of(team.members.begin(), team.members.end(),
        [&](std::uint32_t other) { return members_[other].leader; });
    if (!leader_left && !team.members.empty()) {
        members_[team.members.front()].initiated = true;
        team.zone.reset();
    }
    team.altered = true;
}

// Base 006B91C0 (Recruit): of the house's objects that 006B8DC0 accepts,
// the one nearest to the Zone (or Coord zero) joins. Objects of another
// Group are 0x3200 leptons further away.
void PrototypeAI::recruit(Context& context, Team& team, std::size_t entry) {
    const auto* force = task_force(team_types_[team.type]);
    if (!force || entry >= force->members.size() || team.quantity[entry] >= force->members[entry].first) return;
    const Lepton from = team.zone ? coord_of(*team.zone) : Lepton{};
    const auto& wanted = force->members[entry].second;
    const auto kind = registered(rules_, "InfantryTypes", wanted) ? WorldKind::Infantry :
        registered(rules_, "AircraftTypes", wanted) ? WorldKind::Aircraft : WorldKind::Unit;
    const WorldEntity* best = nullptr;
    std::size_t best_entry = 0;
    std::int64_t best_distance = -1;
    for (const auto* entity : context.actors) {
        if (entity->kind != kind) continue;
        // Vehicles must be the entry type itself; for the other kinds
        // 006B8DC0 finds the entry.
        if (kind == WorldKind::Unit && !equal_name(entity->type, wanted)) continue;
        const auto d = lepton_distance(coord_of(*entity), from);
        std::size_t at = 0;
        if ((best_distance == -1 || d < best_distance) && can_add(context, team, *entity, at)) {
            best = entity;
            best_entry = at;
            best_distance = d;
        }
    }
    if (best) add_member(context, team, best->id, best_entry);
}

// Base 006B88B0: a moving team that falls under strength stops its
// script and goes back to the own building without a weapon nearest to
// its Zone, where it reforms.
// TODO-ORIGINAL: the distance is also times (cell threat + 1), halved for
// a repair bay, and the Cell is a Safety_Point (004BB3F0) near it.
void PrototypeAI::retreat(Context& context, Team& team, std::vector<skirmish::Order>& result) {
    team.moving = false;
    team.line = -1;
    if (team.members.empty()) { team.zone.reset(); return; }
    calc_center(context, team);
    if (!team.zone) return;
    const auto zone = coord_of(*team.zone);
    const WorldEntity* best = nullptr;
    std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
    for (const auto* building : context.buildings) {
        if (!none(rules_.read_string(building->type, "Primary"))) continue;
        const auto d = lepton_distance(coord_of(*building), zone);
        if (d < best_distance) { best_distance = d; best = building; }
    }
    if (best) team.focus = Focus{0, *best->cell};
    coordinate_move(context, team, result);
}

// Base 006B9F40 (Coordinate_Regroup). Returns false when a member far from
// the Zone had to be sent back to it.
bool PrototypeAI::coordinate_regroup(Context& context, Team& team, std::vector<skirmish::Order>& result) {
    bool regrouped = true;
    if (!team.zone) return regrouped;
    const auto zone = coord_of(*team.zone);
    const auto stray = stray_for(rules_, current_action(team));
    for (const auto id : std::vector<std::uint32_t>(team.members)) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        auto& member = members_[id];
        const bool aircraft = found->second->kind == WorldKind::Aircraft;
        const auto d = lepton_distance(coord_of(*found->second), zone);
        if (!member.initiated) {
            if (stray < d) {
                if (!has_navcom(context, id)) assign(context, id, mission_move, *team.zone, 0, result);
            } else member.initiated = true;
        }
        if (!member.initiated && !aircraft) continue;
        if (stray < d && (member.mission != mission_area_guard || !has_tarcom(context, id))) {
            if (!has_navcom(context, id)) {
                regrouped = false;
                assign(context, id, mission_move, *team.zone, member.target, result);
            }
        } else if (member.mission != mission_area_guard) {
            assign(context, id, mission_guard, std::nullopt, member.target, result);
        }
    }
    return regrouped;
}

// Base 006BA1A0 (Coordinate_Move): members first join the ClosestMember;
// joined members go to the Focus. The step is done (IsNextMission) when a
// member was checked against the Focus and every member is within stray
// of it with no NavCom left.
// TODO-ORIGINAL: Lagging_Units (006BA600) runs only while IsLagging (+7C)
// is set, which this team never sets.
void PrototypeAI::coordinate_move(Context& context, Team& team, std::vector<skirmish::Order>& result) {
    if (team.members.empty()) return;
    if (team.focus.empty()) team.focus = team.queued;
    const auto goal = focus_cell(context, team.focus);
    if (!goal) return;
    const auto action = current_action(team);
    const auto stray = stray_for(rules_, action);
    const auto closest = context.own.find(team.closest);
    bool arrived = true, checked = false;
    for (const auto id : std::vector<std::uint32_t>(team.members)) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        auto& member = members_[id];
        const auto& entity = *found->second;
        const bool aircraft = entity.kind == WorldKind::Aircraft;
        if (!member.initiated && closest != context.own.end()) {
            if (stray < lepton_distance(coord_of(entity), coord_of(*closest->second))) {
                if (!has_navcom(context, id)) assign(context, id, mission_move, *closest->second->cell, 0, result);
                arrived = false;
            } else member.initiated = true;
        } else if (!member.initiated) member.initiated = true;
        if (member.mission == mission_unload) arrived = false;
        if (!member.initiated && !aircraft) continue;
        if (member.mission == mission_unload) continue;
        auto reach = stray;
        if (aircraft) reach *= 2;
        checked = true;
        const auto d = lepton_distance(coord_of(entity), coord_of(*goal));
        if (reach < d) {
            // An Aggressive team (+AD) lets a member that has a target fight.
            if (team_types_[team.type].aggressive && has_tarcom(context, id)) continue;
            assign(context, id, mission_move, *goal, 0, result);
            arrived = false;
        } else if (has_navcom(context, id)) {
            arrived = false;
        }
    }
    if (checked && arrived && team.moving) team.next_mission = true;
}

// Base 006B9BB0 (Coordinate_Attack): joined members are set to Attack and
// given the Focus when they have no target of their own. Without a Focus
// or an armed member the step is done.
void PrototypeAI::coordinate_attack(Context& context, Team& team, std::vector<skirmish::Order>& result) {
    if (team.focus.empty()) team.focus = team.queued;
    if (team.focus.object && !context.targets.count(team.focus.object)) team.focus = {};
    if (team.focus.empty()) { team.next_mission = true; return; }
    const auto stray = stray_for(rules_, current_action(team));
    bool attacker = false;
    for (const auto id : std::vector<std::uint32_t>(team.members)) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        auto& member = members_[id];
        const bool aircraft = found->second->kind == WorldKind::Aircraft;
        if (!member.initiated && team.zone) {
            if (stray < lepton_distance(coord_of(*found->second), coord_of(*team.zone))) {
                if (!has_navcom(context, id)) assign(context, id, mission_move, *team.zone, 0, result);
            } else member.initiated = true;
        }
        if (!member.initiated && !aircraft) continue;
        // Set to Attack, then given the Focus when it has no target.
        if (team.focus.object && (member.mission != mission_attack || !has_tarcom(context, id)))
            assign(context, id, mission_attack, std::nullopt, team.focus.object, result);
        else if (!team.focus.object && member.mission != mission_attack)
            assign(context, id, mission_move, focus_cell(context, team.focus), 0, result);
        attacker = true;
    }
    if (!attacker) team.next_mission = true;
}

// Base 006BBBB0 (script action 11): joined members take the mission and
// lose their target and NavCom; members far from the Zone go back to it.
// The step never ends by itself.
void PrototypeAI::coordinate_do(Context& context, Team& team, std::int32_t mission, std::vector<skirmish::Order>& result) {
    const auto stray = stray_for(rules_, current_action(team));
    for (const auto id : std::vector<std::uint32_t>(team.members)) {
        const auto found = context.own.find(id);
        if (found == context.own.end() || !team.zone) continue;
        auto& member = members_[id];
        const bool aircraft = found->second->kind == WorldKind::Aircraft;
        const auto d = lepton_distance(coord_of(*found->second), coord_of(*team.zone));
        if (!member.initiated) {
            if (stray < d) {
                if (!has_navcom(context, id)) assign(context, id, mission_move, *team.zone, 0, result);
            } else member.initiated = true;
        }
        if ((!member.initiated && !aircraft) || has_tarcom(context, id)) continue;
        if (!has_navcom(context, id) && stray * 2 < d && mission != mission_area_guard) {
            assign(context, id, mission_move, *team.zone, 0, result);
            continue;
        }
        if (!has_navcom(context, id) && member.mission != mission &&
            (mission != mission_guard || member.mission != mission_unload))
            assign(context, id, mission, std::nullopt, 0, result);
    }
}

std::int32_t PrototypeAI::current_action(const Team& team) const {
    const auto at = std::find(script_ids_.begin(), script_ids_.end(), team_types_[team.type].script);
    if (at == script_ids_.end() || team.line < 0) return -1;
    const auto& steps = scripts_[static_cast<std::size_t>(at - script_ids_.begin())];
    return team.line < static_cast<std::int32_t>(steps.size()) ? steps[static_cast<std::size_t>(team.line)].action : -1;
}

// Base 006BAA60 (FetchALeader) as the script actions inline it: the
// greatest LeadershipRating (+544), the first member on a tie.
std::uint32_t PrototypeAI::leader(const Context& context, const Team& team) const {
    std::uint32_t best = team.members.empty() ? 0 : team.members.front();
    std::int32_t rating = -1;
    for (const auto id : team.members) {
        const auto found = context.own.find(id);
        if (found == context.own.end()) continue;
        const auto value = rules_.read_integer(found->second->type, "LeadershipRating", 5);
        if (rating < value) { rating = value; best = id; }
    }
    return best;
}

// Base 006BCF70 (enemy) and 006BD240 (own): a building of the type by the
// property in the high word: 0 least threat, 1 greatest threat, 2
// nearest, 3 farthest from the unit; the first building wins a tie.
// TODO-ORIGINAL: cell threat (0054E3A0) is taken as zero, so 0 and 1 give
// the first building. Neutral houses' buildings are not candidates.
const WorldEntity* PrototypeAI::pick_building(const Context& context, std::uint32_t unit, std::int32_t argument,
                                              bool enemy) const {
    const auto from_unit = context.own.find(unit);
    if (from_unit == context.own.end()) return nullptr;
    const auto* registry = rules_.find_section("BuildingTypes");
    const auto index = static_cast<std::size_t>(argument & 0xffff);
    if (!registry || index >= registry->entries.size()) return nullptr;
    const auto& type = registry->entries[index].value;
    const auto property = (argument >> 16) & 0xffff;
    const auto from = coord_of(*from_unit->second);
    const WorldEntity* best = nullptr;
    std::int64_t score = -1;
    const auto consider = [&](const WorldEntity& building) {
        if (building.kind != WorldKind::Structure || !equal_name(building.type, type)) return;
        std::int64_t value = 0;
        const auto d = lepton_distance(coord_of(building), from);
        if (property == 0) value = 0x7fffffff;
        else if (property == 2) value = 0x7fffffff - d;
        else if (property == 3) value = d;
        if (score < value) { score = value; best = &building; }
    };
    if (enemy) for (const auto& [id, target] : context.targets) { (void)id; consider(*target); }
    else for (const auto* building : context.buildings) consider(*building);
    return best;
}

// Base 006BDAA0 (53) and 006BDDB0 (54): a point AISafeDistance cells from
// one base centre toward the other (006B... facing math).
// TODO-ORIGINAL: the binary facing step is not modelled, so the angle is
// not quantized; Nearby_Location (005502E0) is not searched.
std::optional<ra2map::Cell> PrototypeAI::gather_point(Context& context, const Team& team, bool at_enemy) {
    ra2map::Cell from{}, toward{};
    bool random_direction = false;
    const auto lead = context.own.find(leader(context, team));
    const auto centre = lead == context.own.end() ? ra2map::Cell{} : *lead->second->cell;
    if (at_enemy) {
        // With no selected enemy, or an enemy without a base centre, the
        // step is complete.
        if (!context.enemy_available || !context.enemy_centre) return std::nullopt;
        from = *context.enemy_centre;
        toward = context.own_centre ? *context.own_centre : centre;
    } else {
        // TODO-ORIGINAL: an unset own centre is Coord zero.
        from = context.own_centre ? *context.own_centre : centre;
        if (context.enemy_available) toward = context.enemy_centre.value_or(ra2map::Cell{});
        else random_direction = true;
    }
    const auto cells = rules_.read_integer("General", "AISafeDistance", 20);
    const double fx = from.x * 256.0 + 128, fy = from.y * 256.0 + 128;
    // A random facing is a byte shifted into a signed 16-bit facing, then
    // turned into radians as (facing - 0x3FFF) * -2pi/65536.
    const double angle = random_direction ?
        (static_cast<std::int16_t>(random(0, 255) << 8) - 0x3fff) * (-2 * 3.14159265358979323846 / 65536) :
        std::atan2(-(toward.y * 256.0 + 128 - fy), toward.x * 256.0 + 128 - fx);
    return cell_of({fx + std::cos(angle) * cells * 256.0, fy - std::sin(angle) * cells * 256.0, 0});
}

// Base 006B7AD0 (TeamClass::AI), once per frame for each team.
void PrototypeAI::team_ai(Context& context, Team& team, std::vector<skirmish::Order>& result) {
    const auto& type = team_types_[team.type];
    const auto* force = task_force(type);
    // 006B8BB0: strength flags after a member came or went.
    if (team.altered) {
        const bool old_under = team.under;
        std::int32_t desired = 0;
        if (force) for (const auto& entry : force->members) desired += entry.first;
        const auto total = static_cast<std::int32_t>(team.members.size());
        if (total < 1) {
            team.under = true;
            team.full = false;
            team.zone.reset();
            if (team.has_been) { finish(team); return; }
        } else {
            team.full = total == desired;
            if (team.full) team.has_been = true;
            if (!type.reinforce) team.under = !team.has_been;
            else if (desired < 3) team.under = total < desired;
            else team.under = total <= desired / 3;
            team.altered = false;
        }
        if (old_under != team.under) team.reforming = true;
    }
    if (team.moving && team.under) retreat(context, team, result);
    if (!team.moving && (team.full || team.forced_active)) {
        team.moving = true;
        team.has_been = true;
        team.under = false;
        for (const auto id : team.members)
            if (team.reforming || team.forced_active) members_[id].initiated = true;
        team.line = -1;
        team.next_mission = true;
    }
    if (team.reforming || team.moving || !team.zone || !team.closest) calc_center(context, team);
    // A computer house recruits while forming, or while a Reinforce team
    // is not full.
    if (!team.moving || (!team.full && type.reinforce))
        if (force) for (std::size_t i = 0; i < force->members.size(); ++i)
            if (team.quantity[i] < force->members[i].first) recruit(context, team, i);
    if (team.members.empty()) {
        // A skirmish team that never filled is deleted after
        // DissolveUnfilledTeamDelay (Rules +E68).
        if (team.has_been || context.frame - team.created >
                rules_.read_integer("General", "DissolveUnfilledTeamDelay", 5000)) {
            finish(team);
            return;
        }
    }
    if (!team.moving) { coordinate_move(context, team, result); return; }
    if (team.reforming || team.under) {
        team.reforming = !coordinate_regroup(context, team, result);
        return;
    }
    bool first = false;
    if (!team.next_mission) {
        if (team.focus.empty()) team.focus = team.queued;
    } else {
        first = true;
        team.next_mission = false;
        ++team.line;
        const auto at = std::find(script_ids_.begin(), script_ids_.end(), type.script);
        const auto count = at == script_ids_.end() ? 0 : scripts_[static_cast<std::size_t>(at - script_ids_.begin())].size();
        if (team.line >= static_cast<std::int32_t>(count)) { finish(team); return; }
        assign_mission_target(context, team, {}, result);
        team.focus = {};
    }
    run_step(context, team, first, result);
}

// The script actions of base AI.INI, by the 006B7AD0 switch.
void PrototypeAI::run_step(Context& context, Team& team, bool first, std::vector<skirmish::Order>& result) {
    const auto& type = team_types_[team.type];
    const auto at = std::find(script_ids_.begin(), script_ids_.end(), type.script);
    const auto& step = scripts_[static_cast<std::size_t>(at - script_ids_.begin())][static_cast<std::size_t>(team.line)];
    switch (step.action) {
    case 0: { // 006BB480: attack the leader's choice of the quarry.
        if (team.queued.object && !context.targets.count(team.queued.object)) team.queued = {};
        if (team.queued.empty() && !team.members.empty()) {
            // TODO-ORIGINAL: Greatest_Threat (+36C) weighs threat; the
            // nearest object of the quarry kind is taken here.
            const auto lead = context.own.find(leader(context, team));
            const WorldEntity* target = nullptr;
            std::uint64_t range = std::numeric_limits<std::uint64_t>::max();
            if (lead != context.own.end())
                for (const auto& [id, entity] : context.targets) {
                    (void)id;
                    if (!quarry(rules_, step.argument, *entity)) continue;
                    const auto d = distance(*lead->second->cell, *entity->cell);
                    if (d < range) { range = d; target = entity; }
                }
            assign_mission_target(context, team, Focus{target ? target->id : 0, std::nullopt}, result);
            if (team.queued.empty()) team.next_mission = true;
        }
        coordinate_attack(context, team, result);
        break;
    }
    case 5: // Guard: start the timer (+58) with argument * 15 frames.
        if (first) team.guard_until = static_cast<std::int64_t>(context.frame) + step.argument * frames_per_second_team;
        coordinate_regroup(context, team, result);
        if (context.frame >= team.guard_until) team.next_mission = true;
        break;
    case 6: // Jump: lines count from one; the next mission adds one.
        team.line = step.argument - 2;
        team.next_mission = true;
        break;
    case 11: coordinate_do(context, team, step.argument, result); break;
    case 46: // 006BC6D0: attack an enemy building, chosen on the first call.
        if (first && team.queued.empty() && !team.members.empty())
            if (const auto* target = pick_building(context, team.members.front(), step.argument, true))
                assign_mission_target(context, team, Focus{target->id, std::nullopt}, result);
        if (team.queued.object && !context.targets.count(team.queued.object)) team.queued = {};
        if (team.queued.empty()) {
            assign_mission_target(context, team, {}, result);
            team.next_mission = true;
            break;
        }
        coordinate_attack(context, team, result);
        break;
    case 47: case 58: // 006BC7B0, 006BC980: move next to an enemy or own building.
        if (team.queued.empty() && !team.members.empty() && (step.action == 47 || first)) {
            const auto unit = step.action == 47 ? team.members.front() : leader(context, team);
            // TODO-ORIGINAL: Nearby_Location (005502E0) picks a free Cell
            // near the building; the building Cell is used here.
            if (const auto* building = pick_building(context, unit, step.argument, step.action == 47))
                assign_mission_target(context, team, Focus{0, *building->cell}, result);
        }
        if (team.queued.empty()) { team.next_mission = true; break; }
        coordinate_move(context, team, result);
        break;
    case 49: // Register success (+83).
        team.next_mission = true;
        team.succeeded = true;
        break;
    case 53: case 54:
        if (first) {
            const auto point = gather_point(context, team, step.action == 53);
            if (!point) { team.next_mission = true; break; }
            assign_mission_target(context, team, Focus{0, *point}, result);
        }
        coordinate_move(context, team, result);
        break;
    // TODO-ORIGINAL: 8 (unload), 14 (load), 43 (wait until loaded) need
    // transports, 21 (scatter) a scatter order and 55/57 super weapons.
    // They end at once here.
    default:
        team.next_mission = true;
        break;
    }
}
} // namespace ra2::sim
