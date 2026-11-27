#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <set>

namespace hive::coordination {
struct RotationMember {
    std::string id;
    bool rotational{},running{},holding{},turn_complete{};
    std::string acknowledged;
    bool on_request{},swappable{};
    bool available{true};
};
struct SlotAssignment {
    std::string id,command;
    bool main{};
};
// Policy only. Transport, game observations and server joins live outside this class.
class SlotRotation {
    struct Entry {
        SlotAssignment assignment;
        bool occupies{},completed{},rotational{};
        std::uint64_t order{};
    };
    std::map<std::string,Entry> entries_;
    std::set<std::string> residents_;
    std::string visitor_,request_;
    std::uint64_t serial_{},order_{};
    void assign(Entry& entry,bool main);
public:
    std::vector<SlotAssignment> update(const std::vector<RotationMember>& members,unsigned slots,unsigned unidentified,bool request_due=false,const std::string& request={});
};
}
