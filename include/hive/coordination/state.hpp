#pragma once
#include "hive/core/settings.hpp"
#include <chrono>
#include <cstdint>
#include <mutex>

namespace hive::coordination {
using Topics = std::map<std::string,Settings>;
using Clock = std::chrono::steady_clock;
inline constexpr auto peer_expiry=std::chrono::seconds(15);
enum class Role { offline, host, guest };
struct Peer {
    std::string id,name;
    bool host{};
    Topics topics;
    bool operator==(const Peer&) const = default;
};
struct Snapshot {
    Role role{Role::offline};
    std::vector<Peer> peers;
    bool operator==(const Snapshot&) const = default;
};
struct Publication { std::uint64_t revision{}; Topics topics; };
class State {
    mutable std::mutex mutex_;
    Publication local_;
    Snapshot remote_;
    Clock::time_point received_{};
public:
    void publish(std::string topic,Settings value);
    void withdraw(const std::string& topic);
    void clear_publication();
    Publication publication() const;
    std::uint64_t publication_revision() const;
    Snapshot snapshot(Clock::time_point now=Clock::now()) const;
    bool receive(Snapshot value,Clock::time_point now=Clock::now());
    void disconnect();
};
}
