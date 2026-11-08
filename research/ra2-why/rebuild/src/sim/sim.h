#pragma once
#include "../map/map_file.h"
#include "world.h"
#include "ground.h"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {
// Base 00638790/00638840/006388A0; expansion 0065C6D0/0065C780/0065C7E0.
class Random {
public:
    explicit Random(std::uint32_t seed);
    std::uint32_t next();
    // Inclusive; reversed endpoints are swapped. Equal endpoints consume no draw.
    std::int32_t ranged(std::int32_t low, std::int32_t high);
    const std::array<std::uint32_t,250>& table() const { return table_; }
    unsigned first() const { return first_; }
    unsigned second() const { return second_; }
private:
    std::array<std::uint32_t,250> table_{};
    unsigned first_=0, second_=103;
};
using Ini = rules::IniFile;
using Fields = std::map<std::string,std::string>;
struct Type {
    std::string name, category;
    Fields fields; // Authored values, not computed engine defaults.
    std::optional<std::int32_t> strength, cost, speed;
    std::string armor, primary, secondary;
};
struct Weapon {
    std::string name;
    Fields fields;
    std::optional<std::int32_t> damage, rof;
    std::string range, projectile, warhead; // Range conversion is not yet recovered.
};
struct House {
    std::string name, country;
    std::vector<std::string> allies;
    std::optional<std::int32_t> cash; // Unknown for local player money adjustment.
    std::int64_t spent=0;
};
enum class Kind { Structure, Unit, Infantry, Aircraft };
struct Entity {
    std::uint32_t id=0; // Stable native ID; not an original pointer or array index.
    Kind kind=Kind::Unit;
    std::string house, type, mission;
    ra2map::Cell cell;
    std::int32_t facing=0, subcell=0, map_health=0;
    // Unit health scaling recovered from original instructions at 007435DA.
    std::optional<std::int32_t> hp;
    std::optional<Coord> position; // World coordinates are known only with a resolved floor height.
    bool dead=false;
};
enum class Op { TakeCash, ResolvedDamage, RandomRange, Move, Attack, Produce, Stop };
struct Command {
    std::uint64_t frame=0;
    Op op=Op::Stop;
    std::uint32_t entity=0, target=0;
    std::string house, type;
    std::int32_t amount=0, high=0;
    ra2map::Cell destination;
};
enum class Error { None, Unsupported, PastFrame, InvalidEntity, InvalidHouse, InvalidAmount, UnknownHealth, UnknownMoney };
struct Result {
    std::uint64_t ticket=0;
    Error error=Error::None;
    std::int64_t value=0;
    std::string detail;
};
struct SkippedObject { Kind kind; std::string type,house,reason; };
struct Snapshot {
    std::uint64_t frame=0;
    std::vector<Entity> entities;
    std::vector<SkippedObject> skipped_objects;
    std::vector<House> houses;
    std::vector<Result> results; // Commands executed on the last tick.
    std::vector<std::string> gaps;
    std::size_t queued=0;
    std::array<std::uint32_t,250> random_table{};
    unsigned random_first=0, random_second=103;
};
struct Capability { std::string name; bool supported; std::string detail; };
class Simulation {
public:
    // Map INI overlays the base rules. All input objects are copied.
    Simulation(const Ini& rules, const Ini& map, std::uint32_t seed,const TerrainCells& terrain={});
    Result issue(const Command&); // Only accepted commands receive a ticket.
    void tick(); // Execute due native commands in issue order, then increment frame.
    Snapshot snapshot() const;
    const std::map<std::string,Type>& types() const { return types_; }
    const std::map<std::string,Weapon>& weapons() const { return weapons_; }
    static std::vector<Capability> capabilities();
    const TerrainCells& terrain() const { return terrain_; }
private:
    Result execute(const Command&);
    Snapshot state_;
    Random random_;
    TerrainCells terrain_;
    std::map<std::uint32_t,std::size_t> terrain_index_;
    std::uint64_t next_ticket_=1;
    struct Pending { Command command; std::uint64_t ticket; };
    std::vector<Pending> queue_;
    std::map<std::string,Type> types_;
    std::map<std::string,Weapon> weapons_;
};
// Proven factory primitive, 004C9FB0. Input is already resolved build time.
int factory_stage_frames(int resolved_build_frames);
// Cash-only factory update from 004C9B20, with resolved external build inputs.
// This does not establish prerequisites, build-time formulas, or unit placement.
struct Factory {
    int progress=0, remaining_cost=0, interval=0;
    std::uint64_t timer_start=0;
    bool suspended=true, insufficient_funds=false;
    bool done() const { return progress==54; }
    void begin(int resolved_cost,int resolved_build_frames,std::uint64_t frame);
    void tick(House& house,std::uint64_t frame);
};
} // namespace ra2::sim
