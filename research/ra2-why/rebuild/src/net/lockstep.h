#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "../session/world/world.h"
namespace ra2::net {
// Ares-YRpp GeneralDefinitions.h / NetworkEvents.h. Values are original.
enum class EventType : std::uint8_t {
 Empty=0, PowerOn=1, PowerOff=2, Ally=3, MegaMission=4, MegaMissionF=5,
 Idle=6, Scatter=7, Destruct=8, Deploy=9, Detonate=10, Place=11,
 Options=12, GameSpeed=13, Produce=14, Suspend=15, Abandon=16,
 Primary=17, SpecialPlace=18, Exit=19, Animation=20, Repair=21,
 Sell=22, SellCell=23, Special=24, FrameSync=25, Message=26,
 ResponseTime=27, FrameInfo=28, SaveGame=29, Archive=30, AddPlayer=31,
 Timing=32, ProcessTime=33, PageUser=34, RemovePlayer=35, LatencyFudge=36,
 MegaFrameInfo=37, PacketTiming=38, AboutToExit=39, FallbackHost=40,
 AddressChange=41, PlanConnect=42, PlanCommit=43, PlanNodeDelete=44,
 AllCheer=45, AbandonAll=46
};
struct Event {
 EventType type=EventType::Empty;
 std::uint8_t player=0;
 std::uint32_t frame=0;
 // Original union begins at +7 (004C6970); no native struct/pointer wire copies.
 std::array<std::uint8_t,104> data{};
 bool operator==(const Event&) const = default;
};
struct Desync { std::uint32_t frame, player, expected, received; std::string report() const; };
class Lockstep {
public:
 explicit Lockstep(std::uint8_t players, std::uint32_t delay=3);
 std::uint32_t next_frame() const { return next_; }
 std::uint32_t delayed_frame() const;
 // Each player submits exactly one complete batch, including empty batches.
 // Identical retransmissions are accepted; conflicting or late batches fail.
 bool submit(std::uint8_t player, std::uint32_t frame, const std::vector<Event>& events);
 bool ready() const;
 std::vector<Event> advance();
 void checksum(std::uint8_t player, std::uint32_t frame, std::uint32_t crc);
 const std::optional<Desync>& desync() const { return desync_; }
private:
 std::uint8_t players_; std::uint32_t delay_, next_=0;
 std::map<std::uint32_t,std::map<std::uint8_t,std::vector<Event>>> frames_;
 std::map<std::uint32_t,std::map<std::uint8_t,std::uint32_t>> crcs_;
 std::optional<Desync> desync_;
};
// Ares-YRpp Checksummer CRC32, including its partial-word padding.
std::uint32_t frame_crc(std::span<const std::uint8_t> bytes);
// Stable prototype state digest. Call after applying orders and simulating a frame.
std::uint32_t world_crc(const session::world::Snapshot& world);
}
