#include "state.hpp"

namespace hive::coordination {
void State::publish(std::string topic,Settings value) {
    std::lock_guard lock(mutex_);
    auto found=local_.topics.find(topic);
    if(found!=local_.topics.end() && found->second==value) return;
    local_.topics[std::move(topic)]=std::move(value); ++local_.revision;
}
void State::withdraw(const std::string& topic) {
    std::lock_guard lock(mutex_);
    if(local_.topics.erase(topic)) ++local_.revision;
}
void State::clear_publication() { std::lock_guard lock(mutex_); if (!local_.topics.empty()) { local_.topics.clear(); ++local_.revision; } }
Publication State::publication() const { std::lock_guard lock(mutex_);return local_; }
std::uint64_t State::publication_revision() const { std::lock_guard lock(mutex_);return local_.revision; }
Snapshot State::snapshot(Clock::time_point now) const {
    std::lock_guard lock(mutex_);
    return now-received_<peer_expiry?remote_:Snapshot{};
}
bool State::receive(Snapshot value,Clock::time_point now) {
    std::lock_guard lock(mutex_);const bool changed=value!=remote_ || now-received_>=peer_expiry;
    if(changed) remote_=std::move(value);
    received_=now;return changed;
}
void State::disconnect() { std::lock_guard lock(mutex_);remote_={}; }
}
