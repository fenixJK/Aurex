#pragma once
#include "core/runtime/program.hpp"

namespace hive::features::combat {
// Roblox hides chat notifications after inactivity. Open and immediately leave
// the empty entry box; refresh only between movement legs, never while walking.
inline std::unique_ptr<automation::Program> show_chat() {
    class Chat final : public automation::Program {
        unsigned step_{};
    public:
        automation::RunConfig config() const override { return {Time(3000),28,{},false,true}; }
        automation::Step next(const automation::View&) override {
            if (step_++==0) return automation::TapMenuKey{Key::chat,"Show combat notifications"};
            if (step_==2) return automation::TapMenuKey{Key::confirm,"Leave chat entry"};
            return automation::Done{"Combat notifications visible"};
        }
    };
    return std::make_unique<Chat>();
}
}
