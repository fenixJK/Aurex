#pragma once
#include "game/data/join.hpp"

namespace hive::features::vichop {
struct ServerChoice { std::optional<game::JoinTarget> target; std::string status; };
// update runs on the UI thread; next/set_active may run on the automation thread.
// Network I/O never runs on the movement thread.
class ServerDirectory {
public:
    virtual ~ServerDirectory()=default;
    virtual void set_active(bool active)=0;
    virtual void update(Time now)=0;
    virtual ServerChoice next(Time now)=0;
};
std::shared_ptr<ServerDirectory> public_servers();
}
