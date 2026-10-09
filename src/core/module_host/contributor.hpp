#pragma once
#include "core/runtime/program.hpp"
#include "requests.hpp"

namespace hive::automation {
struct ContributorView {
    Time now{};
    std::uint64_t generation{};
    std::optional<Progress> feature;
    game::RequestInbox requests;
};
class Contributor {
public:
    virtual ~Contributor()=default;
    virtual game::Requests requests(const ContributorView& view)=0;
};
}
