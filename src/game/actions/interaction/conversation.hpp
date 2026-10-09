#pragma once
#include "core/runtime/program.hpp"
#include "game/detection/conversation.hpp"

namespace hive::automation {
std::unique_ptr<Program> converse(game::PromptLandmark prompt,std::shared_ptr<const game::ConversationSource> source);
}
