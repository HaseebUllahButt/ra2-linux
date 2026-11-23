#include "prototype_triggers.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

// TODO-ORIGINAL: This is a clear simple version of the map trigger and team
// system. The original code is TActionClass::Execute 006DD8B0 (action numbers
// are its switch cases), TEventClass::HasOccured 0071E940 (event numbers),
// TriggerClass::FireActions 007265C0 and TriggerClass::ResetTimers 00726400
// (elapsed time = p2 seconds * 15 frames), and the TeamClass / ScriptClass
// functions near 006E8A90 / 006913C0. Event and action meanings for numbers
// the original code does not show clearly are marked at their case below.

namespace ra2::sim::triggers {
namespace sw = session::world;
using rules::IniFile;

namespace {
constexpr std::int32_t fps = 15;

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ',') { out.push_back(std::string(IniFile::trim(cur))); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(std::string(IniFile::trim(cur)));
    return out;
}
std::int32_t to_int(const std::string& s, std::int32_t fallback = 0) {
    auto v = IniFile::integer_value(s);
    return v ? *v : fallback;
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool is_none(const std::string& s) { return s.empty() || lower(s) == "<none>"; }

bool alive(const sw::Entity& e) {
    if (e.lifetime != sw::Lifetime::Placed) return false;
    return !e.resolved_hp || *e.resolved_hp > 0;
}
bool mobile(const sw::Entity& e) {
    return e.kind == sw::Kind::Unit || e.kind == sw::Kind::Infantry || e.kind == sw::Kind::Aircraft;
}
int chebyshev(ra2map::Cell a, ra2map::Cell b) {
    return std::max(std::abs((int)a.x - (int)b.x), std::abs((int)a.y - (int)b.y));
}
bool neutral_house(const sw::House& h) {
    return h.role && (*h.role == sw::HouseRole::Neutral || *h.role == sw::HouseRole::Special);
}
} // namespace

std::int32_t waypoint_from_letters(const std::string& s) {
    if (s.empty()) return -1;
    std::int64_t n = 0;
    for (char c : s) {
        if (c < 'A' || c > 'Z') return -1;
        n = n * 26 + (c - 'A' + 1);
    }
    return (std::int32_t)(n - 1);
}

std::int32_t ActionDef::number(int i) const {
    if (i < 0 || i >= 6) return 0;
    return to_int(param[i], 0);
}

// ---- Parsing ---------------------------------------------------------------
MapLogic MapLogic::parse(const IniFile& ini, const IniFile* ai) {
    MapLogic m;
    // Section lists: the map's own entries, then AI.INI entries not yet seen.
    auto ids = [&](const char* list) {
        std::vector<std::pair<std::string,const IniFile*>> out;
        std::set<std::string> seen;
        for (const IniFile* f : {&ini, ai}) {
            if (!f) continue;
            if (auto* sec = f->find_section(list))
                for (const auto& e : sec->entries)
                    if (seen.insert(e.value).second) out.emplace_back(e.value, f);
        }
        return out;
    };
    // The body of an id comes from the map, else from AI.INI.
    auto src = [&](const std::string& id, const IniFile* home) -> const IniFile& {
        if (ini.find_section(id)) return ini;
        if (ai && ai->find_section(id)) return *ai;
        return *home;
    };
    // Houses: names in order, used for house index parameters.
    if (auto* sec = ini.find_section("Houses"))
        for (const auto& e : sec->entries) m.house_names.push_back(e.value);

    // Events and actions are stored by trigger id.
    std::map<std::string, std::vector<EventDef>> events;
    std::map<std::string, std::vector<ActionDef>> actions;
    if (auto* sec = ini.find_section("Events")) {
        for (const auto& e : sec->entries) {
            auto f = split(e.value);
            int n = f.empty() ? 0 : to_int(f[0]);
            auto& list = events[e.key];
            for (int i = 0; i < n; ++i) {
                if ((std::size_t)(3 * i + 3) >= f.size()) break;
                EventDef d;
                d.type = to_int(f[1 + 3 * i]);
                d.p1 = to_int(f[2 + 3 * i]);
                d.p2 = to_int(f[3 + 3 * i]);
                list.push_back(d);
            }
        }
    }
    if (auto* sec = ini.find_section("Actions")) {
        for (const auto& e : sec->entries) {
            auto f = split(e.value);
            int n = f.empty() ? 0 : to_int(f[0]);
            auto& list = actions[e.key];
            for (int i = 0; i < n; ++i) {
                std::size_t base = 1 + 8 * (std::size_t)i;
                if (base + 7 >= f.size()) break;
                ActionDef a;
                a.type = to_int(f[base]);
                for (int k = 0; k < 6; ++k) a.param[k] = f[base + 1 + k];
                a.waypoint = waypoint_from_letters(f[base + 7]);
                list.push_back(a);
            }
        }
    }
    if (auto* sec = ini.find_section("Triggers")) {
        for (const auto& e : sec->entries) {
            auto f = split(e.value);
            if (f.size() < 8) continue;
            TriggerDef t;
            t.id = e.key;
            t.house = f[0];
            t.linked = f[1];
            t.name = f[2];
            t.disabled = to_int(f[3]) != 0;
            t.easy = to_int(f[4]) != 0;
            t.medium = to_int(f[5]) != 0;
            t.hard = to_int(f[6]) != 0;
            int p = to_int(f[7]);
            t.persistence = p >= 2 ? Persistence::Persistent
                          : p == 1 ? Persistence::SemiPersistent : Persistence::Volatile;
            t.events = events[e.key];
            t.actions = actions[e.key];
            m.triggers.push_back(std::move(t));
        }
    }
    if (auto* sec = ini.find_section("Tags")) {
        for (const auto& e : sec->entries) {
            auto f = split(e.value);
            if (f.size() < 3) continue;
            m.tags.push_back(TagDef{e.key, f[1], f[2], to_int(f[0])});
        }
    }
    {
        for (const auto& [id, home] : ids("TaskForces")) {
            const IniFile& s = src(id, home);
            TaskForceDef t;
            t.id = id;
            t.name = s.read_string(t.id, "Name");
            for (std::size_t i = 0; i < 6; ++i) {
                auto v = s.read_string(t.id, std::to_string(i));
                if (v.empty()) continue;
                auto f = split(v);
                if (f.size() >= 2) t.members.emplace_back(to_int(f[0]), f[1]);
            }
            m.task_forces.push_back(std::move(t));
        }
    }
    {
        for (const auto& [id, home] : ids("ScriptTypes")) {
            const IniFile& si = src(id, home);
            ScriptDef s;
            s.id = id;
            s.name = si.read_string(s.id, "Name");
            if (auto* body = si.find_section(s.id)) {
                for (std::size_t i = 0; i < 50; ++i) {
                    const auto* line = si.find(s.id, std::to_string(i));
                    if (!line) break;
                    auto f = split(line->value);
                    ScriptStep st;
                    st.type = f.empty() ? 0 : to_int(f[0]);
                    st.param = f.size() > 1 ? to_int(f[1]) : 0;
                    s.steps.push_back(st);
                }
                (void)body;
            }
            m.scripts.push_back(std::move(s));
        }
    }
    {
        for (const auto& [id, home] : ids("TeamTypes")) {
            const IniFile& ini = src(id, home);   // shadows the map on purpose
            TeamTypeDef t;
            t.id = id;
            t.name = ini.read_string(t.id, "Name");
            t.house = ini.read_string(t.id, "House");
            t.script = ini.read_string(t.id, "Script");
            t.task_force = ini.read_string(t.id, "TaskForce");
            t.waypoint = waypoint_from_letters(ini.read_string(t.id, "Waypoint"));
            t.max = ini.read_integer(t.id, "Max", 1);
            t.priority = ini.read_integer(t.id, "Priority", 0);
            t.autocreate = ini.read_bool(t.id, "Autocreate", false);
            t.suicide = ini.read_bool(t.id, "Suicide", false);
            t.aggressive = ini.read_bool(t.id, "Aggressive", false);
            t.full = ini.read_bool(t.id, "Full", false);
            m.team_types.push_back(std::move(t));
        }
    }
    if (auto* sec = ini.find_section("Waypoints")) {
        for (const auto& e : sec->entries) {
            auto idx = IniFile::integer_value(e.key);
            auto v = IniFile::integer_value(e.value);
            if (!idx || !v || *v < 0) continue;
            m.waypoints[*idx] = ra2map::Cell{(std::uint16_t)(*v % 1000), (std::uint16_t)(*v / 1000)};
        }
    }
    if (auto* sec = ini.find_section("CellTags")) {
        for (const auto& e : sec->entries)
            if (auto k = IniFile::integer_value(e.key)) m.cell_tags[(std::uint32_t)*k] = e.value;
    }
    return m;
}

const TriggerDef* MapLogic::trigger(const std::string& id) const {
    for (const auto& x : triggers) if (x.id == id) return &x;
    return nullptr;
}
const TeamTypeDef* MapLogic::team_type(const std::string& id) const {
    for (const auto& x : team_types) if (x.id == id) return &x;
    return nullptr;
}
const TaskForceDef* MapLogic::task_force(const std::string& id) const {
    for (const auto& x : task_forces) if (x.id == id) return &x;
    return nullptr;
}
const ScriptDef* MapLogic::script(const std::string& id) const {
    for (const auto& x : scripts) if (x.id == id) return &x;
    return nullptr;
}
const TagDef* MapLogic::tag(const std::string& id) const {
    for (const auto& x : tags) if (x.id == id) return &x;
    return nullptr;
}

std::map<std::int32_t,int> count_event_types(const MapLogic& m) {
    std::map<std::int32_t,int> c;
    for (const auto& t : m.triggers) for (const auto& e : t.events) ++c[e.type];
    return c;
}
std::map<std::int32_t,int> count_action_types(const MapLogic& m) {
    std::map<std::int32_t,int> c;
    for (const auto& t : m.triggers) for (const auto& a : t.actions) ++c[a.type];
    return c;
}

// ---- Runtime ---------------------------------------------------------------
struct PrototypeTriggers::Ctx {
    const sw::Snapshot& world;
    std::int32_t frame;
    TickResult out;
    std::map<std::uint32_t, const sw::Entity*> by_id;
    std::set<std::uint32_t> in_team;
    bool bypass_enabled = false;
};

PrototypeTriggers::PrototypeTriggers(const IniFile& map, const IniFile& rules, std::int32_t difficulty,
                                   const IniFile* ai)
    : logic_(MapLogic::parse(map, ai)), rules_(rules), map_(map), difficulty_(difficulty) {
    state_.resize(logic_.triggers.size());
    for (std::size_t i = 0; i < logic_.triggers.size(); ++i) {
        const auto& t = logic_.triggers[i];
        trigger_index_[t.id] = i;
        bool diff = difficulty_ == 0 ? t.easy : difficulty_ == 1 ? t.medium : t.hard;
        state_[i].enabled = !t.disabled && diff;
        if (!diff) state_[i].alive = false;
    }
}

bool PrototypeTriggers::trigger_enabled(const std::string& id) const {
    auto it = trigger_index_.find(id);
    return it != trigger_index_.end() && state_[it->second].alive && state_[it->second].enabled;
}
bool PrototypeTriggers::trigger_alive(const std::string& id) const {
    auto it = trigger_index_.find(id);
    return it != trigger_index_.end() && state_[it->second].alive;
}
bool PrototypeTriggers::global_set(std::int32_t i) const { return i >= 0 && i < 50 && globals_[i]; }
bool PrototypeTriggers::local_set(std::int32_t i) const { return i >= 0 && i < 100 && locals_[i]; }
std::int32_t PrototypeTriggers::mission_timer_frames_left(std::int32_t frame) const {
    if (timer_start_ < 0) return -1;
    return std::max(0, timer_length_ - (frame - timer_start_));
}
std::vector<std::uint32_t> PrototypeTriggers::team_members(std::uint32_t instance) const {
    for (const auto& t : teams_) if (t.instance == instance) return t.members;
    return {};
}
std::vector<std::uint32_t> PrototypeTriggers::team_ids() const {
    std::vector<std::uint32_t> ids;
    for (const auto& t : teams_) ids.push_back(t.instance);
    return ids;
}

void PrototypeTriggers::attach_tag(std::uint32_t entity_id, const std::string& tag_id) {
    entity_tags_[entity_id] = tag_id;
}
void PrototypeTriggers::adopt(std::uint32_t team_instance, std::uint32_t entity_id) {
    for (auto& t : teams_)
        if (t.instance == team_instance) {
            if (std::find(t.members.begin(), t.members.end(), entity_id) == t.members.end())
                t.members.push_back(entity_id);
            return;
        }
}
void PrototypeTriggers::raise(RaisedEvent event, std::uint32_t house_id,
                              std::uint32_t entity_id, ra2map::Cell cell) {
    raised_.emplace_back((std::int32_t)event, house_id, entity_id, cell);
}

std::uint32_t PrototypeTriggers::resolve_house(const sw::Snapshot& w, const std::string& name) const {
    if (is_none(name)) return 0;
    std::string n = name;
    if (auto v = IniFile::integer_value(name)) {
        if (*v >= 0 && *v < (std::int32_t)logic_.house_names.size()) n = logic_.house_names[*v];
        else return 0;
    }
    for (const auto& h : w.houses)
        if (h.name == n || h.name == n + " House" || h.country == n || h.country + " House" == n) return h.id;
    return 0;
}
std::uint32_t PrototypeTriggers::house_by_index(const sw::Snapshot& w, std::int32_t i) const {
    if (i < 0 || i >= (std::int32_t)logic_.house_names.size()) return 0;
    return resolve_house(w, logic_.house_names[i]);
}
std::int32_t PrototypeTriggers::index_of_house(const sw::Snapshot& w, std::uint32_t id) const {
    for (std::size_t i = 0; i < logic_.house_names.size(); ++i)
        if (resolve_house(w, logic_.house_names[i]) == id) return (std::int32_t)i;
    return -1;
}
int PrototypeTriggers::type_index(const std::string& list, const std::string& type) const {
    if (auto* sec = rules_.find_section(list))
        for (std::size_t i = 0; i < sec->entries.size(); ++i)
            if (sec->entries[i].value == type) return (int)i;
    return -1;
}
std::string PrototypeTriggers::type_name(const std::string& list, std::int32_t index) const {
    if (auto* sec = rules_.find_section(list))
        if (index >= 0 && (std::size_t)index < sec->entries.size()) return sec->entries[index].value;
    return {};
}

// Event numbers (TEventClass::HasOccured 0071E940). A bit in `occurred`
// remembers events raised by objects; the others are checked every frame.
bool PrototypeTriggers::event_true(const Ctx& c, std::size_t ti, std::size_t ei) {
    const auto& trig = logic_.triggers[ti];
    const auto& ev = trig.events[ei];
    const auto& st = state_[ti];
    const auto& w = c.world;
    const std::uint32_t house = resolve_house(w, trig.house);
    auto raised = [&] { return ei < 64 && ((st.occurred >> ei) & 1); };
    auto house_of_p2 = [&] {
        auto h = house_by_index(w, ev.p2);
        return h ? h : house;
    };
    auto count = [&](std::uint32_t h, auto pred) {
        int n = 0;
        for (const auto& e : w.entities) if (e.house_id == h && pred(e)) ++n;
        return n;
    };
    switch (ev.type) {
    case 0: return false;
    case 13: // Elapsed time: p2 seconds after the trigger was (re)armed.
    case 51: // TODO-ORIGINAL 0071E940 case 0x33: original adds a random part. Plain p2 here.
        return st.timer_start >= 0 && c.frame - st.timer_start >= ev.p2 * fps;
    case 47: // Seconds since scenario start (0071E940 case 0x2f: p2 <= frame/15).
        return ev.p2 <= c.frame / fps;
    case 14: return timer_running_ && timer_expired_;
    case 8:  // Any event: the original returns true without a check.
        return true;
    case 27: return global_set(ev.p2);
    case 28: return !global_set(ev.p2);
    case 36: return local_set(ev.p2);
    case 37: return !local_set(ev.p2);
    case 12: // Credits at least p2 (0071E940 case 0xc).
        for (const auto& h : w.houses) if (h.id == house) return h.cash && *h.cash >= ev.p2;
        return false;
    case 52: // Credits at most p2 (case 0x34).
        for (const auto& h : w.houses) if (h.id == house) return h.cash && *h.cash <= ev.p2;
        return false;
    case 9: { // All units of the house are gone.
        auto h = house_of_p2();
        return h && count(h, [](const sw::Entity& e) { return (e.kind == sw::Kind::Unit || e.kind == sw::Kind::Infantry) && alive(e); }) == 0;
    }
    case 10: { // All buildings of the house are gone.
        auto h = house_of_p2();
        return h && count(h, [](const sw::Entity& e) { return e.kind == sw::Kind::Structure && alive(e); }) == 0;
    }
    case 11: { // Everything of the house is gone.
        auto h = house_of_p2();
        return h && count(h, [](const sw::Entity& e) { return e.kind != sw::Kind::Terrain && alive(e); }) == 0;
    }
    case 15: { // Destroyed buildings count at least p2.
        return count(house, [](const sw::Entity& e) { return e.kind == sw::Kind::Structure && e.lifetime == sw::Lifetime::Destroyed; }) >= ev.p2;
    }
    case 16:
        return count(house, [](const sw::Entity& e) { return (e.kind == sw::Kind::Unit || e.kind == sw::Kind::Infantry) && e.lifetime == sw::Lifetime::Destroyed; }) >= ev.p2;
    case 17: { // No factories left.
        return count(house, [&](const sw::Entity& e) {
            return e.kind == sw::Kind::Structure && alive(e) && !is_none(rules_.read_string(e.type, "Factory"));
        }) == 0;
    }
    case 19: case 20: case 21: case 22: { // The house built something of type index p2.
        static const char* lists[] = {"BuildingTypes", "VehicleTypes", "InfantryTypes", "AircraftTypes"};
        auto name = type_name(lists[ev.type - 19], ev.p2);
        auto it = built_.find(house);
        return !name.empty() && it != built_.end() && it->second.count(name) && it->second.at(name) > 0;
    }
    case 30: { // Low power.
        for (const auto& h : w.houses)
            if (h.id == house && h.power_output && h.power_drain) return *h.power_output < *h.power_drain;
        return false;
    }
    case 32: case 57: { // Building of type index p2 exists / does not exist.
        auto name = type_name("BuildingTypes", ev.p2);
        bool exists = !name.empty() && count(house, [&](const sw::Entity& e) { return e.type == name && alive(e); }) > 0;
        return ev.type == 32 ? exists : !exists;
    }
    default:
        // Events that objects raise: entered, attacked, destroyed, discovered,
        // bridge destroyed, selected, damage levels, ...
        // TODO-ORIGINAL 0071E940: 25/26 (line crossing), 34 (near waypoint),
        // 24 (zone entry), 35, 45, 46, 48, 49, 50 are only true when raised.
        return raised();
    }
}

void PrototypeTriggers::fire(Ctx& c, std::size_t ti) {
    auto& st = state_[ti];
    const auto& trig = logic_.triggers[ti];
    st.fired_once = true;
    for (const auto& a : trig.actions) run_action(c, ti, a);
    // Linked trigger: TODO-ORIGINAL 007265C0. Treated as "enable this one next".
    if (!is_none(trig.linked)) {
        auto it = trigger_index_.find(trig.linked);
        if (it != trigger_index_.end() && state_[it->second].alive) {
            state_[it->second].enabled = true;
            state_[it->second].timer_start = c.frame;
        }
    }
    bool polled_only = true;
    for (const auto& e : trig.events)
        switch (e.type) { case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 24: case 25: case 26:
        case 29: case 31: case 33: case 34: case 35: case 38: case 39: case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 48: polled_only = false; break; default: break; }
    switch (trig.persistence) {
    case Persistence::Volatile: st.alive = false; break;
    case Persistence::SemiPersistent: // TODO-ORIGINAL: one fire, then wait for new object events.
        if (polled_only) st.alive = false;
        st.occurred = 0;
        break;
    case Persistence::Persistent:
        st.occurred = 0;
        st.timer_start = c.frame;
        break;
    }
}

bool PrototypeTriggers::create_team(Ctx& c, const std::string& type_id, std::uint32_t house, bool spawn) {
    const auto* td = logic_.team_type(type_id);
    if (!td) return false;
    const auto* tf = logic_.task_force(td->task_force);
    if (!tf) return false;
    int running = 0;
    std::size_t type_idx = (std::size_t)(td - logic_.team_types.data());
    for (const auto& t : teams_) if (t.type == type_idx) ++running;
    if (running >= std::max(td->max, 1)) return false;
    auto team_house = resolve_house(c.world, td->house);
    if (!team_house) team_house = house;
    Team team;
    team.instance = next_team_++;
    team.house = team_house;
    team.type = type_idx;
    team.wait_until = c.frame;
    if (spawn) {
        ra2map::Cell where{};
        auto wp = logic_.waypoints.find(td->waypoint);
        if (wp != logic_.waypoints.end()) where = wp->second;
        for (const auto& m : tf->members)
            for (int i = 0; i < m.first; ++i) {
                Effect e;
                e.kind = EffectKind::Spawn;
                e.house = team_house;
                e.cell = where;
                e.text = m.second;
                e.team = team.instance;
                c.out.effects.push_back(e);
                ++team.spawn_expected;
            }
        team.pending_spawn = team.spawn_expected > 0;
        team.wait_until = c.frame + 150;   // give the match time to adopt members
        if (!team.pending_spawn) return false;
    } else {
        // Recruit loose units of the team's house, as many as the task force lists.
        for (const auto& m : tf->members) {
            int need = m.first;
            for (const auto& e : c.world.entities) {
                if (need <= 0) break;
                if (e.house_id != team_house || e.type != m.second || !mobile(e) || !alive(e)) continue;
                if (c.in_team.count(e.id)) continue;
                team.members.push_back(e.id);
                c.in_team.insert(e.id);
                --need;
            }
        }
        if (team.members.empty()) return false;
    }
    teams_.push_back(std::move(team));
    return true;
}

// Action numbers are the switch cases of TActionClass::Execute 006DD8B0.
bool PrototypeTriggers::run_action(Ctx& c, std::size_t ti, const ActionDef& a) {
    const auto& trig = logic_.triggers[ti];
    const auto& w = c.world;
    const std::uint32_t house = resolve_house(w, trig.house);
    auto fx = [&](EffectKind k) {
        Effect e;
        e.kind = k;
        e.house = house;
        return e;
    };
    auto wp_cell = [&](ra2map::Cell& cell) {
        auto it = logic_.waypoints.find(a.waypoint);
        if (it == logic_.waypoints.end()) return false;
        cell = it->second;
        return true;
    };
    auto trigger_by_id = [&](const std::string& id) -> std::size_t {
        auto it = trigger_index_.find(id);
        return it == trigger_index_.end() ? (std::size_t)-1 : it->second;
    };
    switch (a.type) {
    case 0: return true;
    case 1: { auto e = fx(EffectKind::Win); c.out.effects.push_back(e); return true; }   // 006E0440
    case 2: { auto e = fx(EffectKind::Lose); c.out.effects.push_back(e); return true; }  // 006E0460
    case 14: { // Winner is: p2 is a house index.
        auto e = fx(EffectKind::Win);
        if (auto h = house_by_index(w, a.number(1))) e.house = h;
        c.out.effects.push_back(e);
        return true;
    }
    case 3: c.out.effects.push_back(fx(EffectKind::BeginProduction)); return true;
    case 13: c.out.effects.push_back(fx(EffectKind::BeginAutocreate)); return true;
    case 9: c.out.effects.push_back(fx(EffectKind::FireSale)); return true;
    case 4: return create_team(c, a.param[1], house, false);
    case 7: case 80: // Reinforcement (80 reaches the same team code at 0065D8E0).
        return create_team(c, a.param[1], house, true);
    case 5: { // Destroy team: disband every running team of that type.
        const auto* td = logic_.team_type(a.param[1]);
        if (!td) return false;
        std::size_t idx = (std::size_t)(td - logic_.team_types.data());
        teams_.erase(std::remove_if(teams_.begin(), teams_.end(), [&](const Team& t) { return t.type == idx; }), teams_.end());
        return true;
    }
    case 6: { // All to hunt: every mobile unit of the house attacks the nearest enemy.
        for (const auto& e : w.entities) {
            if (e.house_id != house || !mobile(e) || !alive(e) || !e.cell) continue;
            const sw::Entity* best = nullptr; int bd = 1 << 30;
            for (const auto& o : w.entities) {
                if (o.house_id == house || !o.house_id || !alive(o) || !o.cell || o.kind == sw::Kind::Terrain) continue;
                bool ally = false;
                for (const auto& h : w.houses) if (h.id == o.house_id && neutral_house(h)) ally = true;
                if (ally) continue;
                int d = chebyshev(*e.cell, *o.cell);
                if (d < bd) { bd = d; best = &o; }
            }
            if (best) {
                skirmish::Order o;
                o.kind = skirmish::OrderKind::Attack; o.house = house; o.entity = e.id;
                o.target = best->id; o.cell = *best->cell;
                c.out.orders.push_back(o);
            }
        }
        return true;
    }
    case 8: { auto e = fx(EffectKind::DropFlare); wp_cell(e.cell); c.out.effects.push_back(e); return true; }
    case 10: { auto e = fx(EffectKind::PlayMovie); e.number = a.number(1); c.out.effects.push_back(e); return true; }
    case 11: case 103: { auto e = fx(EffectKind::ShowText); e.text = a.param[1]; c.out.effects.push_back(e); return true; }
    case 12: { // Destroy trigger
        auto i = trigger_by_id(a.param[1]);
        if (i == (std::size_t)-1) return false;
        state_[i].alive = false;
        return true;
    }
    case 16: c.out.effects.push_back(fx(EffectKind::RevealAll)); return true;
    case 17: case 18: { // Reveal around waypoint / reveal zone
        auto e = fx(a.type == 17 ? EffectKind::RevealAround : EffectKind::RevealZone);
        if (!wp_cell(e.cell)) return false;
        e.number = rules_.read_integer("General", "RevealTriggerRadius", 3);
        c.out.effects.push_back(e);
        return true;
    }
    case 19: { auto e = fx(EffectKind::PlaySound); e.text = a.param[1]; c.out.effects.push_back(e); return true; }
    case 20: { auto e = fx(EffectKind::PlayMusic); e.text = a.param[1]; c.out.effects.push_back(e); return true; }
    case 21: { auto e = fx(EffectKind::PlaySpeech); e.text = a.param[1]; c.out.effects.push_back(e); return true; }
    case 100: { auto e = fx(EffectKind::PlaySpeech); e.number = a.number(1); c.out.effects.push_back(e); return true; }
    case 99: case 101: { // Sound at a waypoint (99 by name; 101 by index. TODO-ORIGINAL 101)
        auto e = fx(EffectKind::SoundAt); e.text = a.param[1]; e.number = a.number(1); wp_cell(e.cell);
        c.out.effects.push_back(e);
        return true;
    }
    case 22: { // Force trigger: run its actions now.
        auto i = trigger_by_id(a.param[1]);
        if (i == (std::size_t)-1 || !state_[i].alive || i == ti) return false;
        fire(c, i);
        return true;
    }
    case 23: timer_start_ = c.frame; timer_length_ = std::max(timer_length_, 0); timer_running_ = true; timer_expired_ = false;
        c.out.effects.push_back(fx(EffectKind::TimerStarted)); return true;
    case 24: timer_running_ = false; c.out.effects.push_back(fx(EffectKind::TimerStopped)); return true;
    case 25: // extend (p2 seconds)
        if (timer_start_ < 0) { timer_start_ = c.frame; timer_length_ = 0; }
        timer_length_ += a.number(1) * fps; timer_expired_ = false; return true;
    case 26:
        timer_length_ = std::max(0, timer_length_ - a.number(1) * fps); return true;
    case 27: // set and start
        timer_start_ = c.frame; timer_length_ = a.number(1) * fps; timer_running_ = true; timer_expired_ = false;
        c.out.effects.push_back(fx(EffectKind::TimerStarted)); return true;
    case 28: { int i = a.number(1); if (i < 0 || i >= 50) return false; globals_[i] = true; return true; }
    case 29: { int i = a.number(1); if (i < 0 || i >= 50) return false; globals_[i] = false; return true; }
    case 56: { int i = a.number(1); if (i < 0 || i >= 100) return false; locals_[i] = true; return true; }
    case 57: { int i = a.number(1); if (i < 0 || i >= 100) return false; locals_[i] = false; return true; }
    case 32: { auto e = fx(EffectKind::DestroyAttached); c.out.effects.push_back(e); return true; }
    case 36: { auto e = fx(EffectKind::ChangeHouse); e.other = house_by_index(w, a.number(1)); c.out.effects.push_back(e); return true; }
    case 37: case 38: { // Make ally / make enemy with house index p2
        auto e = fx(a.type == 37 ? EffectKind::SetAlly : EffectKind::SetEnemy);
        e.other = house_by_index(w, a.number(1));
        c.out.effects.push_back(e);
        return true;
    }
    case 40: { auto e = fx(EffectKind::ResizeView); e.number = a.number(1); c.out.effects.push_back(e); return true; }
    case 41: { auto e = fx(EffectKind::PlayAnim); e.number = a.number(1); wp_cell(e.cell); c.out.effects.push_back(e); return true; }
    case 42: { auto e = fx(EffectKind::Explosion); e.number = a.number(1); wp_cell(e.cell); c.out.effects.push_back(e); return true; }
    case 44: c.out.effects.push_back(fx(EffectKind::IonStormStart)); return true;
    case 45: c.out.effects.push_back(fx(EffectKind::IonStormEnd)); return true;
    case 46: c.out.effects.push_back(fx(EffectKind::LockInput)); return true;
    case 47: c.out.effects.push_back(fx(EffectKind::UnlockInput)); return true;
    case 48: case 55: { // Move camera to waypoint (0x30, 0x37)
        auto e = fx(EffectKind::CenterView);
        if (!wp_cell(e.cell)) return false;
        c.out.effects.push_back(e);
        return true;
    }
    case 51: c.out.effects.push_back(fx(EffectKind::Reshroud)); return true;
    case 52: { auto e = fx(EffectKind::Lighting); e.number = a.number(1); c.out.effects.push_back(e); return true; }
    case 53: case 54: { // Enable / disable a trigger (006E2AF0 and the case after it)
        auto i = trigger_by_id(a.param[1]);
        if (i == (std::size_t)-1 || !state_[i].alive) return false;
        state_[i].enabled = (a.type == 53);
        if (a.type == 53) { state_[i].timer_start = c.frame; state_[i].occurred = 0; }
        return true;
    }
    case 107: { // Team of type p2 moves to a waypoint (0x6B). TODO-ORIGINAL 0065EC30.
        const auto* td = logic_.team_type(a.param[1]);
        ra2map::Cell cell{};
        if (!td || !wp_cell(cell)) return false;
        std::size_t idx = (std::size_t)(td - logic_.team_types.data());
        for (auto& t : teams_) if (t.type == idx) {
            for (auto id : t.members) {
                skirmish::Order o;
                o.kind = skirmish::OrderKind::Move; o.house = t.house; o.entity = id; o.cell = cell;
                c.out.orders.push_back(o);
            }
        }
        return true;
    }
    // TODO-ORIGINAL: 74, 104, 108, 112-117 appear in maps but their handlers
    // (006E1980 and neighbours, 0065EC30, 0056BEC0) are not rebuilt. They
    // report success and do nothing.
    default: return true;
    }
}

// ---- Teams and scripts -----------------------------------------------------
namespace {
bool target_kind_match(const IniFile& rules, int kind, const sw::Entity& e) {
    const auto& t = e.type;
    switch (kind) {
    case 2: return e.kind == sw::Kind::Structure;
    case 3: return e.kind == sw::Kind::Unit && rules.read_bool(t, "Harvester", false);
    case 4: return e.kind == sw::Kind::Infantry;
    case 5: return e.kind == sw::Kind::Unit && !rules.read_bool(t, "Harvester", false);
    case 6: return e.kind == sw::Kind::Structure && !is_none(rules.read_string(t, "Factory"));
    case 7: return e.kind == sw::Kind::Structure && !is_none(rules.read_string(t, "Primary"));
    case 9: return e.kind == sw::Kind::Structure && rules.read_integer(t, "Power", 0) > 0;
    case 11: return e.kind == sw::Kind::Structure && rules.read_bool(t, "Capturable", false);
    default: return e.kind != sw::Kind::Terrain;   // 1 and anything else: anything
    }
}
} // namespace

void PrototypeTriggers::run_teams(Ctx& c) {
    const auto& w = c.world;
    for (auto& e : w.entities) c.by_id[e.id] = &e;
    for (std::size_t ti = 0; ti < teams_.size(); ++ti) {
        auto& team = teams_[ti];
        team.members.erase(std::remove_if(team.members.begin(), team.members.end(), [&](std::uint32_t id) {
            auto it = c.by_id.find(id);
            return it == c.by_id.end() || !alive(*it->second);
        }), team.members.end());
        for (auto id : team.members) c.in_team.insert(id);
        if (team.pending_spawn) {
            if ((std::int32_t)team.members.size() >= team.spawn_expected || c.frame >= team.wait_until)
            { team.pending_spawn = false; team.wait_until = c.frame; }
            else continue;
        }
        if (team.members.empty()) { team.done = true; continue; }
        const auto* td = &logic_.team_types[team.type];
        const auto* script = logic_.script(td->script);
        if (!script) { team.done = true; continue; }
        auto centre = [&]() -> ra2map::Cell {
            auto it = c.by_id.find(team.members.front());
            return it != c.by_id.end() && it->second->cell ? *it->second->cell : ra2map::Cell{};
        };
        auto order_all = [&](skirmish::OrderKind k, std::uint32_t target, ra2map::Cell cell) {
            for (auto id : team.members) {
                skirmish::Order o;
                o.kind = k; o.house = team.house; o.entity = id; o.target = target; o.cell = cell;
                c.out.orders.push_back(o);
            }
        };
        for (int guard = 0; guard < 64; ++guard) {
            if (c.frame < team.wait_until) break;
            if (team.pc < 0 || team.pc >= (std::int32_t)script->steps.size()) { team.done = true; break; }
            const auto step = script->steps[team.pc];
            switch (step.type) {
            case 3: case 16: case 1: { // Move / patrol / attack-move to waypoint
                if (!team.moving) {
                    auto it = logic_.waypoints.find(step.param);
                    if (it == logic_.waypoints.end()) { ++team.pc; continue; }
                    team.goal = it->second;
                    order_all(skirmish::OrderKind::Move, 0, team.goal);
                    team.moving = true;
                    team.wait_until = c.frame + 15;
                    goto next_team_step_yield;
                }
                bool arrived = true;
                for (auto id : team.members) {
                    auto it = c.by_id.find(id);
                    if (it != c.by_id.end() && it->second->cell && chebyshev(*it->second->cell, team.goal) > 3) arrived = false;
                }
                // TODO-ORIGINAL: the original also ends when the team is stuck.
                if (arrived || c.frame >= team.wait_until + 1800) { team.moving = false; ++team.pc; continue; }
                team.wait_until = c.frame + 15;
                goto next_team_step_yield;
            }
            case 0: { // Attack a kind of target
                if (team.target) {
                    auto it = c.by_id.find(team.target);
                    if (it == c.by_id.end() || !alive(*it->second)) { team.target = 0; ++team.pc; continue; }
                    // Re-issue now and then so new members join in.
                    order_all(skirmish::OrderKind::Attack, team.target, it->second->cell ? *it->second->cell : ra2map::Cell{});
                    team.wait_until = c.frame + 30;
                    goto next_team_step_yield;
                }
                const sw::Entity* best = nullptr; int bd = 1 << 30;
                auto mine = centre();
                for (const auto& o : w.entities) {
                    if (o.house_id == team.house || !o.house_id || !alive(o) || !o.cell) continue;
                    bool skip = false;
                    for (const auto& h : w.houses) if (h.id == o.house_id && neutral_house(h)) skip = true;
                    if (skip || !target_kind_match(rules_, step.param, o)) continue;
                    int d = chebyshev(mine, *o.cell);
                    if (d < bd) { bd = d; best = &o; }
                }
                if (!best) { ++team.pc; continue; }
                team.target = best->id;
                order_all(skirmish::OrderKind::Attack, best->id, *best->cell);
                team.wait_until = c.frame + 30;
                goto next_team_step_yield;
            }
            case 5: // Guard for param seconds. TODO-ORIGINAL unit: ScriptClass guard time.
                if (!team.moving) { team.moving = true; team.wait_until = c.frame + std::max(step.param, 1) * fps; goto next_team_step_yield; }
                team.moving = false; ++team.pc; continue;
            case 6: team.pc = step.param; continue;      // Jump to line
            case 9: order_all(skirmish::OrderKind::Deploy, 0, ra2map::Cell{}); ++team.pc; continue;
            case 7: case 23: { // Player wins
                Effect e; e.kind = EffectKind::Win; e.house = team.house; c.out.effects.push_back(e);
                ++team.pc; continue;
            }
            case 24: case 25: case 27: case 26: { // speech / sound / music / movie
                ++team.pc; continue;
            }
            case 37: { // Delete team members
                for (auto id : team.members) { Effect e; e.kind = EffectKind::DeleteMember; e.other = id; c.out.effects.push_back(e); }
                team.members.clear(); team.done = true; break;
            }
            case 49: team.wait_until = c.frame + 1; ++team.pc; goto next_team_step_yield;
            default:
                // TODO-ORIGINAL ScriptClass actions 2 4 8 10 11 12 13 14 15 18-22 28-36 38-48 50-58:
                // skipped without effect.
                ++team.pc; continue;
            }
            break;
        }
    next_team_step_yield:;
    }
    teams_.erase(std::remove_if(teams_.begin(), teams_.end(), [](const Team& t) { return t.done; }), teams_.end());
}

TickResult PrototypeTriggers::tick(const sw::Snapshot& world, std::int32_t frame) {
    if (last_frame_ >= 0 && frame <= last_frame_) {
        if (frame < last_frame_) last_frame_ = -1;   // rewind: start clocks again below
        else return {};
    }
    Ctx c{world, frame, {}, {}, {}, false};
    for (auto& e : world.entities) c.by_id[e.id] = &e;

    if (last_frame_ < 0) {
        // First frame: arm timers and bind map tags to objects.
        for (auto& s : state_) s.timer_start = frame;
        if (timer_start_ < 0) timer_start_ = -1;
        for (const auto& sec_name : {"Structures", "Units", "Infantry", "Aircraft"}) {
            auto* sec = map_.find_section(sec_name);
            if (!sec) continue;
            int tag_idx = std::string(sec_name) == "Structures" ? 6
                        : std::string(sec_name) == "Infantry" ? 8 : 7;
            for (const auto& l : sec->entries) {
                auto f = split(l.value);
                if ((int)f.size() <= tag_idx || is_none(f[tag_idx])) continue;
                int x = to_int(f[3]), y = to_int(f[4]);
                for (const auto& e : world.entities)
                    if (e.type == f[1] && e.cell && e.cell->x == x && e.cell->y == y && !entity_tags_.count(e.id))
                        entity_tags_[e.id] = f[tag_idx];
            }
        }
        for (const auto& e : world.entities) seen_.insert(e.id);
        built_.clear();
    } else {
        // Count things that appeared after the first frame as "built".
        for (const auto& e : world.entities)
            if (!seen_.count(e.id)) {
                seen_.insert(e.id);
                if (alive(e)) ++built_[e.house_id][e.type];
            }
    }
    last_frame_ = frame;

    // Mission timer
    if (timer_running_ && timer_start_ >= 0 && frame - timer_start_ >= timer_length_) {
        timer_expired_ = true;
        timer_running_ = true;
    }

    // Cell tags: a unit entering a tagged cell raises "entered by".
    for (const auto& e : world.entities) {
        if (!mobile(e) || !alive(e) || !e.cell) continue;
        auto key = (std::uint32_t)e.cell->y * 1000 + e.cell->x;
        auto it = logic_.cell_tags.find(key);
        if (it != logic_.cell_tags.end() && !entered_.count({e.id, key})) {
            entered_.insert({e.id, key});
            raised_.emplace_back((std::int32_t)RaisedEvent::EnteredBy, e.house_id, e.id, *e.cell);
        }
    }

    // Deliver raised events: object events go through tags, others to all.
    for (const auto& r : raised_) {
        auto [type, house, entity, cell] = r;
        std::set<std::size_t> targets;
        auto add_tag = [&](const std::string& tag_id) {
            if (const auto* tag = logic_.tag(tag_id)) {
                auto it = trigger_index_.find(tag->trigger);
                if (it != trigger_index_.end()) targets.insert(it->second);
            }
        };
        bool object_event = type == (std::int32_t)RaisedEvent::EnteredBy || type <= 7 || (type >= 38 && type <= 46);
        if (entity) {
            auto it = entity_tags_.find(entity);
            if (it != entity_tags_.end()) add_tag(it->second);
        }
        auto ck = logic_.cell_tags.find((std::uint32_t)cell.y * 1000 + cell.x);
        if (type == (std::int32_t)RaisedEvent::EnteredBy && ck != logic_.cell_tags.end()) add_tag(ck->second);
        if (!object_event || (!entity && ck == logic_.cell_tags.end()))
            for (std::size_t i = 0; i < logic_.triggers.size(); ++i) targets.insert(i);
        for (auto i : targets) {
            const auto& t = logic_.triggers[i];
            for (std::size_t ei = 0; ei < t.events.size() && ei < 64; ++ei) {
                if (t.events[ei].type != type) continue;
                // p2 may name the house that must cause the event.
                auto want = house_by_index(world, t.events[ei].p2);
                if (type == 1 && want && want != house) continue;
                state_[i].occurred |= std::uint64_t(1) << ei;
            }
        }
    }
    raised_.clear();

    // Evaluate triggers in file order. A trigger fires when every event is true.
    for (std::size_t i = 0; i < logic_.triggers.size(); ++i) {
        auto& st = state_[i];
        const auto& t = logic_.triggers[i];
        if (!st.alive || !st.enabled || t.events.empty()) continue;
        bool all = true;
        for (std::size_t ei = 0; ei < t.events.size() && all; ++ei) all = event_true(c, i, ei);
        if (all) fire(c, i);
    }

    run_teams(c);
    return std::move(c.out);
}

} // namespace ra2::sim::triggers
