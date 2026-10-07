#pragma once
#include "program.hpp"
#include "hive/module_host/requests.hpp"

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
