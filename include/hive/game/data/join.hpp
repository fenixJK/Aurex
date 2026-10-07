#pragma once
#include "hive/core/model.hpp"

namespace hive::game {
class JoinTarget {
    std::string uri_;
public:
    explicit JoinTarget(const std::string& private_server_link={});
    static JoinTarget from_uri(const std::string& uri);
    static JoinTarget follow_user(const std::string& user_id);
    const std::string& uri() const { return uri_; }
};
}
