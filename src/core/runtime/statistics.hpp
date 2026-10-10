#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <atomic>

namespace hive {
enum class WorkActivity { other, gathering, travelling, converting, recovering, fighting };
struct Counter { std::string id,label; std::uint64_t value{},epoch{}; std::string summary; };
struct Threshold { std::string id; double minimum{}; };
class Counters {
    struct Entry { Counter counter; std::uint64_t previous{},source{}; bool seen{}; };
    inline static std::atomic<std::uint64_t> next_{};
    std::uint64_t epoch_{++next_};
    std::map<std::string,Entry> entries_;
public:
    void observe(std::string id,std::string label,std::uint64_t value,std::uint64_t source,std::string summary={}) {
        auto& entry=entries_[id];
        const auto delta=!entry.seen || entry.source!=source || value<entry.previous?value:value-entry.previous;
        entry.counter={std::move(id),std::move(label),entry.counter.value+delta,epoch_,std::move(summary)};
        entry.previous=value; entry.source=source; entry.seen=true;
    }
    std::vector<Counter> values() const {
        std::vector<Counter> result; for (const auto& [id,entry]:entries_) result.push_back(entry.counter); return result;
    }
};
struct RecoveryCounts { std::uint64_t disconnects{},rejoins{}; bool disconnected{}; };
}
