#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/conversation.hpp"

namespace hive::automation {
std::unique_ptr<Program> converse(std::shared_ptr<const game::ConversationSource> source);
}
