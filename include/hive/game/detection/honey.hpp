#pragma once
#include "perception.hpp"
namespace hive::game {
class HoneyFilter {
    Reading<std::uint64_t> candidate_,confirmed_;
public:
    Reading<std::uint64_t> observe(const ObservationContext& context,Reading<std::uint64_t> reading);
    void reset() { candidate_={}; confirmed_={}; }
};
}
