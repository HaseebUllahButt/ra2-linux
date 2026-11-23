#pragma once
#include "lockstep.h"
#include <memory>
namespace ra2::net {
struct Player {
 std::string name;
 std::uint8_t side=0, color=0, start_position=0;
 bool operator==(const Player&) const = default;
};
struct Lobby { std::string map; std::vector<Player> players; bool started=false; };
struct LanGame { std::string address; std::uint16_t port; Lobby lobby; };
// Native Linux UDP protocol, not compatible with original Westwood network packets.
// Host owns lobby. poll() receives packets and repeats recent data to repair loss.
class LanSession {
public:
 static std::unique_ptr<LanSession> host(std::uint16_t port, Player player, std::string map);
 static std::unique_ptr<LanSession> join(const std::string& address, std::uint16_t port, Player player);
 static std::vector<LanGame> discover(std::uint16_t port, int timeout_ms=200,
                                     const std::string& address="example.invalid");
 ~LanSession();
 LanSession(const LanSession&)=delete;
 LanSession& operator=(const LanSession&)=delete;
 void poll();
 const Lobby& lobby() const;
 int player_id() const;
 std::uint16_t port() const;
 void start();
 bool send_orders(std::uint32_t frame, const std::vector<Event>& events);
 void send_crc(std::uint32_t frame, std::uint32_t crc);
 Lockstep& lockstep(); // Available after lobby.started; apply returned orders to shared world.
private:
 LanSession();
 struct Impl; std::unique_ptr<Impl> impl_;
};
}
